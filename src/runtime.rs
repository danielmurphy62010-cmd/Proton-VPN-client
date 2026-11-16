use crate::{
    api::{self, ApiCredentials, ProtonServer},
    config::{AppConfig, FinalFallback, Mode, Profile},
    metrics::ProfileMetrics,
    process::{self, stop_child},
};
use anyhow::{bail, Context, Result};
use axum::{
    body::Body,
    extract::{Request, State},
    http::StatusCode,
    middleware::{self, Next},
    response::{IntoResponse, Response},
    routing::{get, post},
    Json, Router,
};
use rand::{seq::SliceRandom, thread_rng};
use serde::{Deserialize, Serialize};
use std::{
    collections::HashMap,
    fs,
    path::PathBuf,
    sync::Arc,
    time::Duration,
};
use tokio::{
    net::{TcpListener, TcpStream},
    process::{Child, Command},
    sync::{oneshot, Mutex, RwLock},
};

#[derive(Clone)]
pub struct AppState {
    pub config: Arc<RwLock<AppConfig>>,
    runtime: Arc<Mutex<RuntimeState>>,
    credentials: Arc<RwLock<Option<ApiCredentials>>>,
    servers: Arc<RwLock<Vec<ProtonServer>>>,
    proton_snapshot: Arc<RwLock<Option<api::ProtonSnapshot>>>,
    vpn_credentials: Arc<RwLock<Option<Arc<api::ProtonVpnCredentials>>>>,
}
struct RuntimeState {
    status: String,
    message: String,
    active: Option<ActiveConnection>,
    vpn_child: Option<Child>,
    tor_child: Option<Child>,
    active_connection_counted: bool,
    metrics: HashMap<String, ProfileMetrics>,
}
#[derive(Clone, Debug, Serialize)]
pub struct ActiveConnection {
    pub profile_id: Option<String>,
    pub profile_name: String,
    pub protocol: Mode,
    pub route: String,
    pub since: String,
}
#[derive(Serialize)]
pub struct StatusView {
    pub status: String,
    pub message: String,
    pub active: Option<ActiveConnection>,
    pub tor_running: bool,
    pub proxy_bind: String,
    pub api_session_present: bool,
    pub concurrent_vpn_sessions_supported: bool,
}
#[derive(Clone)]
pub struct ApiState {
    pub app: AppState,
    pub token: String,
    pub quit: Arc<Mutex<Option<oneshot::Sender<()>>>>,
}

#[derive(Deserialize)]
pub struct SessionPayload { pub access_token: String, pub uid: String }
#[derive(Deserialize)]
pub struct ConnectPayload { pub profile_id: Option<String> }
#[derive(Deserialize)]
pub struct ImportPayload {
    pub config_file: PathBuf,
    pub protocol: Mode,
    pub name: Option<String>,
    pub country_code: Option<String>,
}

impl AppState {
    pub fn new(config: AppConfig) -> Self {
        Self {
            config: Arc::new(RwLock::new(config)),
            runtime: Arc::new(Mutex::new(RuntimeState {
                status: "starting".into(), message: "Starting Tor and local proxy".into(),
                active: None, vpn_child: None, tor_child: None,
                active_connection_counted: false, metrics: HashMap::new(),
            })),
            credentials: Arc::new(RwLock::new(None)),
            servers: Arc::new(RwLock::new(Vec::new())),
            proton_snapshot: Arc::new(RwLock::new(None)),
            vpn_credentials: Arc::new(RwLock::new(None)),
        }
    }

    pub async fn status(&self) -> StatusView {
        let cfg = self.config.read().await.clone();
        let rt = self.runtime.lock().await;
        let api_session_present = self.credentials.read().await.is_some();
        StatusView {
            status: rt.status.clone(), message: rt.message.clone(), active: rt.active.clone(),
            tor_running: rt.tor_child.is_some(), proxy_bind: cfg.proxy_bind,
            api_session_present,
            // Independent simultaneous VPN routes require distinct TUN interfaces and policy routing.
            concurrent_vpn_sessions_supported: false,
        }
    }


    pub async fn ensure_tor_ready(&self) -> Result<()> {
        self.start_tor_if_needed().await?;
        for _ in 0..80 {
            match TcpStream::connect("127.0.0.1:9050").await {
                Ok(stream) => {
                    drop(stream);
                    return Ok(());
                }
                Err(_) => tokio::time::sleep(Duration::from_millis(250)).await,
            }
        }
        bail!("Tor SOCKS5 did not become ready on 127.0.0.1:9050; Proton API requests are blocked rather than sent directly.");
    }

    pub async fn route_for_proxy(&self) -> ProxyRoute {
        let cfg = self.config.read().await;
        let rt = self.runtime.lock().await;
        if let Some(active) = &rt.active {
            match active.route.as_str() {
                "tor" => ProxyRoute::Tor,
                _ => ProxyRoute::SystemRoute,
            }
        } else {
            match &cfg.final_fallback {
                FinalFallback::Tor => ProxyRoute::Tor,
                FinalFallback::Lan => ProxyRoute::SystemRoute,
            }
        }
    }

    async fn start_tor_if_needed(&self) -> Result<()> {
        let cfg = self.config.read().await.clone();
        let mut rt = self.runtime.lock().await;
        let still_running = match rt.tor_child.as_mut() {
            Some(child) => child.try_wait()?.is_none(),
            None => false,
        };
        if still_running { return Ok(()); }
        rt.tor_child = None;

        if !cfg.tor_executable.is_file() {
            if rt.active.as_ref().map(|a| a.route.as_str()) == Some("tor") {
                rt.status = "fallback_unavailable".into();
            }
            rt.message = format!("Tor executable not found: {}", cfg.tor_executable.display());
            return Ok(());
        }
        let tor_bin_dir = cfg.tor_executable.parent()
            .context("Tor executable path has no parent directory")?;
        let tor_root = tor_bin_dir.parent()
            .context("Tor executable must be inside Tor/tor/tor.exe")?;
        let tor_data_dir = tor_root.join("data");
        let tor_state_dir = tor_root.join("state");
        fs::create_dir_all(&tor_state_dir)?;
        if !tor_bin_dir.join("tor.exe").is_file() {
            bail!("Tor executable not found at the configured portable path: {}", cfg.tor_executable.display());
        }

        let mut command = Command::new(&cfg.tor_executable);
        // Preserve the Tor Expert Bundle layout:
        // Tor/tor/tor.exe, Tor/data/{geoip,geoip6}, Tor/state.
        command.current_dir(tor_bin_dir);
        command.args(["--SocksPort", "127.0.0.1:9050", "--DataDirectory"])
            .arg(&tor_state_dir)
            .args(["--ClientOnly", "1", "--Log", "notice stdout"]);
        let geoip = tor_data_dir.join("geoip");
        let geoip6 = tor_data_dir.join("geoip6");
        if geoip.is_file() {
            command.arg("--GeoIPFile").arg(&geoip);
        }
        if geoip6.is_file() {
            command.arg("--GeoIPv6File").arg(&geoip6);
        }
        command.kill_on_drop(true);
        match command.spawn() {
            Ok(child) => {
                rt.tor_child = Some(child);
                if rt.active.is_none() { rt.status = "running".into(); }
                rt.message = "Tor is running on SOCKS5 127.0.0.1:9050".into();
                Ok(())
            }
            Err(error) => {
                rt.message = format!("Could not start Tor: {error}");
                Err(error.into())
            }
        }
    }

    pub async fn stop_all(&self) {
        let mut rt = self.runtime.lock().await;
        if let Some(mut child) = rt.vpn_child.take() { stop_child(&mut child).await; }
        if let Some(mut child) = rt.tor_child.take() { stop_child(&mut child).await; }
        rt.active = None;
        rt.active_connection_counted = false;
        rt.status = "stopped".into();
        rt.message = "All managed child processes were stopped".into();
    }
}

#[derive(Clone, Copy)]
pub enum ProxyRoute { Tor, SystemRoute }

pub async fn build_router(api_state: ApiState) -> Router {
    Router::new()
        .route("/health", get(|| async { "ok" }))
        .route("/api/config", get(get_config).put(put_config))
        .route("/api/status", get(get_status))
        .route("/api/auth/status", get(auth_status))
        .route("/api/auth/session", post(set_session))
        .route("/api/servers", get(get_servers))
        .route("/api/servers/refresh", post(refresh_servers))
        .route("/api/proton/snapshot", get(get_proton_snapshot))
        .route("/api/profiles/import", post(import_profile))
        .route("/api/profiles/remove", post(remove_profile))
        .route("/api/connect", post(connect))
        .route("/api/disconnect", post(disconnect))
        .route("/api/metrics", get(get_metrics))
        .route("/api/quit", post(quit))
        .route_layer(middleware::from_fn_with_state(api_state.clone(), authorize))
        .with_state(api_state)
}

async fn authorize(State(state): State<ApiState>, request: Request<Body>, next: Next) -> Response {
    let provided = request.headers().get("x-client-token").and_then(|v| v.to_str().ok());
    if provided != Some(state.token.as_str()) {
        return (StatusCode::UNAUTHORIZED, "invalid local client token").into_response();
    }
    next.run(request).await
}
async fn get_config(State(state): State<ApiState>) -> Json<AppConfig> {
    Json(state.app.config.read().await.clone())
}
async fn put_config(State(state): State<ApiState>, Json(config): Json<AppConfig>) -> Result<Json<AppConfig>, (StatusCode, String)> {
    config.validate().map_err(api_error)?;
    config.save().map_err(api_error)?;
    // Configuration changes affect future connection attempts, not the active child.
    *state.app.config.write().await = config.clone();
    Ok(Json(config))
}
async fn get_status(State(state): State<ApiState>) -> Json<StatusView> {
    Json(state.app.status().await)
}
async fn auth_status(State(state): State<ApiState>) -> Json<serde_json::Value> {
    Json(serde_json::json!({ "present": state.app.credentials.read().await.is_some() }))
}
async fn set_session(State(state): State<ApiState>, Json(payload): Json<SessionPayload>) -> Result<Json<serde_json::Value>, (StatusCode, String)> {
    if payload.access_token.trim().is_empty() || payload.uid.trim().is_empty() {
        return Err((StatusCode::BAD_REQUEST, "Access token and UID are required".into()));
    }
    if payload.access_token.len() > 8192 || payload.uid.len() > 512 {
        return Err((StatusCode::BAD_REQUEST, "Session fields exceed allowed size".into()));
    }
    *state.app.credentials.write().await = Some(ApiCredentials {
        access_token: payload.access_token, uid: payload.uid,
    });
    *state.app.vpn_credentials.write().await = None;
    *state.app.proton_snapshot.write().await = None;
    *state.app.servers.write().await = Vec::new();
    Ok(Json(serde_json::json!({ "present": true, "storage": "memory_only" })))
}
async fn get_servers(State(state): State<ApiState>) -> Json<Vec<ProtonServer>> {
    Json(state.app.servers.read().await.clone())
}
async fn refresh_servers(State(state): State<ApiState>) -> Result<Json<Vec<ProtonServer>>, (StatusCode, String)> {
    let credentials = state.app.credentials.read().await.clone()
        .ok_or_else(|| (StatusCode::UNAUTHORIZED, "No Proton API access token and UID are loaded.".into()))?;
    state.app.ensure_tor_ready().await.map_err(api_error)?;
    let cfg = state.app.config.read().await.clone();
    let snapshot = api::fetch_snapshot(&cfg.proton_api_base, &credentials).await.map_err(api_error)?;
    let servers = snapshot.servers.clone();

    let cached_status = state.app.vpn_credentials.read().await.as_ref().map(|value| value.status.expiration_time);
    let now = chrono::Utc::now().timestamp();
    if cached_status.map(|expiry| expiry <= now + 300).unwrap_or(true) {
        let vpn_credentials = api::request_connection_credentials(&cfg.proton_api_base, &credentials)
            .await.map_err(api_error)?;
        *state.app.vpn_credentials.write().await = Some(Arc::new(vpn_credentials));
    }

    *state.app.servers.write().await = servers.clone();
    *state.app.proton_snapshot.write().await = Some(snapshot);
    Ok(Json(servers))
}

async fn get_proton_snapshot(State(state): State<ApiState>) -> Result<Json<api::ProtonSnapshot>, (StatusCode, String)> {
    state.app.proton_snapshot.read().await.clone()
        .map(Json)
        .ok_or_else(|| (StatusCode::NOT_FOUND, "Proton account/server data has not been fetched yet. Refresh the Proton server list first.".into()))
}
async fn import_profile(State(state): State<ApiState>, Json(payload): Json<ImportPayload>) -> Result<Json<Profile>, (StatusCode, String)> {
    if !matches!(&payload.protocol, Mode::OpenVpn | Mode::WireGuard | Mode::Stealth) {
        return Err((StatusCode::BAD_REQUEST, "Only OpenVPN, WireGuard, and Stealth profile files can be imported".into()));
    }
    if !payload.config_file.is_file() {
        return Err((StatusCode::BAD_REQUEST, "Configuration file does not exist".into()));
    }
    let (host, port) = extract_endpoint(&payload.config_file, &payload.protocol).map_err(api_error)?;
    let name = payload.name.filter(|x| !x.trim().is_empty())
        .or_else(|| payload.config_file.file_stem().map(|s| s.to_string_lossy().into_owned()))
        .unwrap_or_else(|| "Imported profile".into());
    let profile = Profile {
        id: uuid::Uuid::new_v4().to_string(), name, country_code: payload.country_code.unwrap_or_default(),
        protocol: payload.protocol, endpoint_host: host, endpoint_port: port,
        config_file: Some(payload.config_file), enabled: true,
    };
    let mut cfg = state.app.config.write().await;
    cfg.profiles.push(profile.clone());
    cfg.save().map_err(api_error)?;
    Ok(Json(profile))
}
async fn remove_profile(State(state): State<ApiState>, Json(payload): Json<serde_json::Value>) -> Result<Json<serde_json::Value>, (StatusCode, String)> {
    let id = payload.get("profile_id").and_then(|v| v.as_str()).unwrap_or_default();
    let mut cfg = state.app.config.write().await;
    cfg.profiles.retain(|p| p.id != id);
    cfg.save().map_err(api_error)?;
    Ok(Json(serde_json::json!({ "removed": id })))
}
async fn connect(State(state): State<ApiState>, Json(payload): Json<ConnectPayload>) -> Result<Json<StatusView>, (StatusCode, String)> {
    connect_internal(&state.app, payload.profile_id.as_deref()).await.map_err(api_error)?;
    Ok(Json(state.app.status().await))
}
async fn disconnect(State(state): State<ApiState>) -> Result<Json<StatusView>, (StatusCode, String)> {
    let mut rt = state.app.runtime.lock().await;
    if let Some(mut child) = rt.vpn_child.take() { stop_child(&mut child).await; }
    rt.active = None;
    rt.active_connection_counted = false;
    rt.status = "disconnected".into();
    rt.message = "VPN profile disconnected; Tor remains running".into();
    drop(rt);
    Ok(Json(state.app.status().await))
}
async fn get_metrics(State(state): State<ApiState>) -> Json<HashMap<String, ProfileMetrics>> {
    Json(state.app.runtime.lock().await.metrics.clone())
}
async fn quit(State(state): State<ApiState>) -> Json<serde_json::Value> {
    if let Some(tx) = state.quit.lock().await.take() { let _ = tx.send(()); }
    Json(serde_json::json!({ "shutting_down": true }))
}

fn api_error(error: impl std::fmt::Display) -> (StatusCode, String) {
    (StatusCode::BAD_GATEWAY, error.to_string())
}

fn extract_endpoint(path: &std::path::Path, protocol: &Mode) -> Result<(Option<String>, Option<u16>)> {
    let text = fs::read_to_string(path).with_context(|| format!("reading profile {}", path.display()))?;
    for line in text.lines().map(str::trim) {
        if line.starts_with('#') || line.starts_with(';') { continue; }
        match protocol {
            Mode::OpenVpn if line.starts_with("remote ") => {
                let mut parts = line.split_whitespace();
                let _ = parts.next();
                if let Some(host) = parts.next() {
                    let port = parts.next().and_then(|p| p.parse::<u16>().ok());
                    return Ok((Some(host.to_string()), port));
                }
            }
            Mode::WireGuard | Mode::Stealth if line.starts_with("Endpoint") => {
                if let Some((_, value)) = line.split_once('=') {
                    let value = value.trim();
                    if let Some((host, port)) = value.rsplit_once(':') {
                        return Ok((Some(host.trim_matches(|c| c == '[' || c == ']')
                            .to_string()), port.parse().ok()));
                    }
                }
            }
            _ => {}
        }
    }
    Ok((None, None))
}

pub async fn connect_internal(app: &AppState, requested_id: Option<&str>) -> Result<()> {
    {
        let rt = app.runtime.lock().await;
        if rt.active.is_some() {
            bail!("A route is already active. Disconnect it manually before changing profiles or protocols.");
        }
    }
    let cfg = app.config.read().await.clone();
    match &cfg.mode {
        Mode::Tor => {
            app.start_tor_if_needed().await?;
            let mut rt = app.runtime.lock().await;
            if rt.tor_child.is_none() { bail!("Tor could not be started; see the status message for the configured executable path."); }
            rt.active = Some(ActiveConnection { profile_id: None, profile_name: "Tor".into(),
                protocol: Mode::Tor, route: "tor".into(), since: chrono::Utc::now().to_rfc3339() });
            rt.active_connection_counted = false;
            rt.status = "connected".into();
            rt.message = "HTTP proxy traffic is routed through Tor SOCKS5".into();
            return Ok(());
        }
        Mode::Lan => {
            let mut rt = app.runtime.lock().await;
            rt.active = Some(ActiveConnection { profile_id: None, profile_name: "LAN / direct".into(),
                protocol: Mode::Lan, route: "system".into(), since: chrono::Utc::now().to_rfc3339() });
            rt.active_connection_counted = false;
            rt.status = "connected".into();
            rt.message = "HTTP proxy traffic uses the normal Windows route".into();
            return Ok(());
        }
        _ => {}
    }

    let weights = if cfg.benchmark.enabled { &cfg.benchmark.priority } else { &cfg.priority };
    let mut candidates: Vec<Profile> = cfg.profiles.iter()
        .filter(|p| p.enabled && requested_id.map(|id| p.id == id).unwrap_or(true))
        .filter(|p| cfg.countries.is_empty() || cfg.countries.iter().any(|c| c.eq_ignore_ascii_case(&p.country_code)))
        .filter(|p| cfg.mode == Mode::Smart || p.protocol == cfg.mode)
        .cloned().collect();
    let stats = app.runtime.lock().await.metrics.clone();
    candidates.sort_by(|a, b| {
        let score = |p: &Profile| stats.get(&p.id).map(|m| m.priority_score(weights)).unwrap_or(0.0);
        score(b).partial_cmp(&score(a)).unwrap_or(std::cmp::Ordering::Equal)
    });
    candidates.truncate(cfg.fallback_attempts as usize);

    let mut failures = Vec::new();
    for profile in candidates {
        let mode = if cfg.mode == Mode::Smart { profile.protocol.clone() } else { cfg.mode.clone() };
        match process::spawn_profile_process(&mode, &profile, &cfg).await {
            Ok(child) => {
                let mut rt = app.runtime.lock().await;
                rt.vpn_child = Some(child);
                rt.active_connection_counted = false;
                rt.active = Some(ActiveConnection {
                    profile_id: Some(profile.id.clone()), profile_name: profile.name.clone(),
                    protocol: mode.clone(), route: "system".into(), since: chrono::Utc::now().to_rfc3339(),
                });
                rt.status = "connecting".into();
                rt.message = format!("Started {} profile '{}'. Tunnel readiness and OS route must still be verified.", mode_name(&mode), profile.name);
                return Ok(());
            }
            Err(error) => {
                failures.push(format!("{}: {error}", profile.name));
                app.runtime.lock().await.metrics.entry(profile.id.clone()).or_default().record_connection(false);
            }
        }
    }

    let mut rt = app.runtime.lock().await;
    match cfg.final_fallback {
        FinalFallback::Tor => {
            drop(rt);
            app.start_tor_if_needed().await?;
            let mut rt = app.runtime.lock().await;
            rt.active = Some(ActiveConnection { profile_id: None, profile_name: "Tor fallback".into(),
                protocol: Mode::Tor, route: "tor".into(), since: chrono::Utc::now().to_rfc3339() });
            rt.active_connection_counted = false;
            rt.status = if rt.tor_child.is_some() { "fallback".into() } else { "fallback_unavailable".into() };
            rt.message = format!("No VPN profile started. Tor fallback selected. {}", failures.join(" | "));
        }
        FinalFallback::Lan => {
            rt.active = Some(ActiveConnection { profile_id: None, profile_name: "LAN fallback".into(),
                protocol: Mode::Lan, route: "system".into(), since: chrono::Utc::now().to_rfc3339() });
            rt.active_connection_counted = false;
            rt.status = "fallback".into();
            rt.message = format!("No VPN profile started. LAN/direct fallback selected. {}", failures.join(" | "));
        }
    }
    Ok(())
}

fn mode_name(mode: &Mode) -> &'static str {
    match mode { Mode::Smart => "Smart", Mode::OpenVpn => "OpenVPN", Mode::WireGuard => "WireGuard",
        Mode::Stealth => "Stealth", Mode::Tor => "Tor", Mode::Lan => "LAN" }
}

pub async fn run_supervisors(app: AppState) {
    let mut last_ping = tokio::time::Instant::now();
    let mut last_rotation = tokio::time::Instant::now();
    loop {
        let cfg = app.config.read().await.clone();

        // Reap VPN children that exit after an initially successful process start.
        let mut vpn_exit = None;
        {
            let mut rt = app.runtime.lock().await;
            if let Some(child) = rt.vpn_child.as_mut() {
                if let Ok(Some(status)) = child.try_wait() {
                    vpn_exit = Some(status.to_string());
                }
            }
            if vpn_exit.is_some() {
                if let Some(active) = rt.active.take() {
                    if let Some(profile_id) = active.profile_id {
                        rt.metrics.entry(profile_id).or_default().record_connection(false);
                    }
                }
                rt.vpn_child = None;
                rt.active_connection_counted = false;
                rt.status = "fallback_pending".into();
                rt.message = format!("VPN process exited unexpectedly: {}", vpn_exit.as_deref().unwrap_or("unknown"));
            }
        }
        if let Some(exit) = vpn_exit {
            match &cfg.final_fallback {
                FinalFallback::Tor => {
                    if let Err(error) = app.start_tor_if_needed().await {
                        tracing::warn!(%error, "Tor fallback could not start");
                    }
                    let mut rt = app.runtime.lock().await;
                    let tor_available = rt.tor_child.is_some();
                    rt.active = Some(ActiveConnection { profile_id: None, profile_name: "Tor fallback".into(),
                        protocol: Mode::Tor, route: "tor".into(), since: chrono::Utc::now().to_rfc3339() });
                    rt.status = if tor_available { "fallback".into() } else { "fallback_unavailable".into() };
                    rt.message = format!("VPN process exited ({exit}); HTTP proxy moved to Tor fallback");
                }
                FinalFallback::Lan => {
                    let mut rt = app.runtime.lock().await;
                    rt.active = Some(ActiveConnection { profile_id: None, profile_name: "LAN fallback".into(),
                        protocol: Mode::Lan, route: "system".into(), since: chrono::Utc::now().to_rfc3339() });
                    rt.status = "fallback".into();
                    rt.message = format!("VPN process exited ({exit}); HTTP proxy moved to LAN/direct fallback");
                }
            }
        }

        if let Err(error) = app.start_tor_if_needed().await {
            tracing::warn!(%error, "Tor supervisor could not start Tor");
        }

        let ping_interval = if cfg.benchmark.enabled {
            cfg.benchmark.ping_interval_seconds
        } else { cfg.ping_interval_seconds };
        if last_ping.elapsed() >= Duration::from_secs(ping_interval.max(1)) {
            last_ping = tokio::time::Instant::now();
            let active = app.runtime.lock().await.active.clone();
            if let Some(active) = active {
                if let Some(profile_id) = active.profile_id {
                    let route = if active.route == "tor" { ProxyRoute::Tor } else { ProxyRoute::SystemRoute };
                    let result = probe_cloudflare(route).await;
                    let mut rt = app.runtime.lock().await;
                    let should_record_success = result.is_some() && !rt.active_connection_counted;
                    {
                        let metrics = rt.metrics.entry(profile_id.clone()).or_default();
                        metrics.record_probe(result, "tcp_connect_1.1.1.1:443");
                        if should_record_success { metrics.record_connection(true); }
                        let path = PathBuf::from("./data/metrics").join(format!("{profile_id}.json"));
                        let _ = metrics.save(&path);
                    }
                    if should_record_success {
                        rt.active_connection_counted = true;
                        rt.status = "connected".into();
                        rt.message = "VPN process is alive and the active route passed a TCP reachability probe; verify the actual tunnel route separately.".into();
                    }
                }
            }
        }

        let benchmark_mode_supports_rotation = matches!(
            &cfg.mode, Mode::Smart | Mode::OpenVpn | Mode::WireGuard | Mode::Stealth
        );
        if cfg.benchmark.enabled
            && benchmark_mode_supports_rotation
            && last_rotation.elapsed() >= Duration::from_secs(cfg.benchmark.rotate_after_seconds.max(1))
        {
            last_rotation = tokio::time::Instant::now();
            let current = app.runtime.lock().await.active.as_ref().and_then(|a| a.profile_id.clone());
            let stats = app.runtime.lock().await.metrics.clone();
            let mut candidates: Vec<Profile> = cfg.profiles.iter()
                .filter(|p| p.enabled)
                .filter(|p| cfg.countries.is_empty() || cfg.countries.iter().any(|c| c.eq_ignore_ascii_case(&p.country_code)))
                .filter(|p| cfg.mode == Mode::Smart || p.protocol == cfg.mode)
                .filter(|p| Some(p.id.clone()) != current)
                .cloned()
                .collect();
            // Rank the candidates with the benchmark-only weights, then choose
            // randomly within the higher-ranked half to retain benchmark variety.
            candidates.sort_by(|a, b| {
                let score = |p: &Profile| stats.get(&p.id)
                    .map(|m| m.priority_score(&cfg.benchmark.priority)).unwrap_or(0.0);
                score(b).partial_cmp(&score(a)).unwrap_or(std::cmp::Ordering::Equal)
            });
            let ranked_pool = ((candidates.len() + 1) / 2).max(1).min(candidates.len());
            candidates.truncate(ranked_pool);
            candidates.shuffle(&mut thread_rng());
            if let Some(profile) = candidates.into_iter().next() {
                {
                    let mut rt = app.runtime.lock().await;
                    if let Some(mut child) = rt.vpn_child.take() { stop_child(&mut child).await; }
                    rt.active = None;
                }
                let _ = connect_internal(&app, Some(&profile.id)).await;
            }
        }
        tokio::time::sleep(Duration::from_millis(500)).await;
    }
}

async fn probe_cloudflare(route: ProxyRoute) -> Option<Duration> {
    let started = tokio::time::Instant::now();
    let result = tokio::time::timeout(Duration::from_secs(4), async {
        match route {
            ProxyRoute::Tor => {
                let stream = tokio_socks::tcp::Socks5Stream::connect("127.0.0.1:9050", "1.1.1.1:443")
                    .await
                    .map_err(|error| std::io::Error::new(std::io::ErrorKind::Other, error.to_string()))?;
                drop(stream);
                Ok::<(), std::io::Error>(())
            }
            ProxyRoute::SystemRoute => {
                let stream = TcpStream::connect("1.1.1.1:443").await?;
                drop(stream);
                Ok::<(), std::io::Error>(())
            }
        }
    }).await;
    match result { Ok(Ok(())) => Some(started.elapsed()), _ => None }
}

pub async fn serve_api(app: AppState, token: String) -> Result<(tokio::task::JoinHandle<()>, oneshot::Receiver<()>, u16)> {
    let (quit_tx, quit_rx) = oneshot::channel();
    let api_state = ApiState { app, token, quit: Arc::new(Mutex::new(Some(quit_tx))) };
    let router = build_router(api_state).await;
    let listener = TcpListener::bind("127.0.0.1:0").await?;
    let port = listener.local_addr()?.port();
    let handle = tokio::spawn(async move {
        if let Err(error) = axum::serve(listener, router).await {
            tracing::error!(%error, "local API server stopped");
        }
    });
    Ok((handle, quit_rx, port))
}

pub async fn serve_proxy(app: AppState, bind: String) -> Result<tokio::task::JoinHandle<()>> {
    let listener = TcpListener::bind(&bind).await.with_context(|| format!("binding local HTTP proxy at {bind}"))?;
    tracing::info!(%bind, "local HTTP CONNECT proxy listening");
    Ok(tokio::spawn(async move {
        loop {
            match listener.accept().await {
                Ok((stream, _)) => {
                    let app = app.clone();
                    tokio::spawn(async move {
                        if let Err(error) = crate::proxy::handle_client(stream, app).await {
                            tracing::debug!(%error, "proxy client ended");
                        }
                    });
                }
                Err(error) => tracing::warn!(%error, "proxy accept failed"),
            }
        }
    }))
}
