use anyhow::{bail, Context, Result};
use directories::ProjectDirs;
use serde::{Deserialize, Serialize};
use std::{fs, net::SocketAddr, path::PathBuf};

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Mode { Smart, OpenVpn, WireGuard, Stealth, Tor, Lan }
impl Default for Mode { fn default() -> Self { Self::Smart } }

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum FinalFallback { Tor, Lan }
impl Default for FinalFallback { fn default() -> Self { Self::Tor } }

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct PriorityWeights {
    pub latency: f64,
    pub successful_connections: f64,
    pub failed_connections: f64,
    pub connection_count: f64,
    pub success_failure_ratio: f64,
}
impl Default for PriorityWeights {
    fn default() -> Self {
        Self { latency: 1.0, successful_connections: 0.2, failed_connections: 1.0,
            connection_count: 0.1, success_failure_ratio: 0.5 }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct BenchmarkConfig {
    pub enabled: bool,
    pub ping_interval_seconds: u64,
    pub rotate_after_seconds: u64,
    pub priority: PriorityWeights,
}
impl Default for BenchmarkConfig {
    fn default() -> Self {
        Self { enabled: false, ping_interval_seconds: 1, rotate_after_seconds: 30,
            priority: PriorityWeights::default() }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct Profile {
    pub id: String,
    pub name: String,
    pub country_code: String,
    pub protocol: Mode,
    pub endpoint_host: Option<String>,
    pub endpoint_port: Option<u16>,
    pub config_file: Option<PathBuf>,
    pub enabled: bool,
}
impl Default for Profile {
    fn default() -> Self {
        Self { id: String::new(), name: String::new(), country_code: String::new(),
            protocol: Mode::OpenVpn, endpoint_host: None, endpoint_port: None,
            config_file: None, enabled: true }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct AppConfig {
    pub mode: Mode,
    pub countries: Vec<String>,
    pub profiles: Vec<Profile>,
    pub fallback_attempts: u8,
    pub final_fallback: FinalFallback,
    pub ping_interval_seconds: u64,
    pub priority: PriorityWeights,
    pub benchmark: BenchmarkConfig,
    pub proton_api_base: String,
    pub proxy_bind: String,
    pub tor_executable: PathBuf,
    pub openvpn_executable: PathBuf,
    pub wireguard_go_executable: PathBuf,
    pub wg_executable: PathBuf,
    pub stealth_executable: Option<PathBuf>,
    pub allow_external_tunnel_commands: bool,
}
impl Default for AppConfig {
    fn default() -> Self {
        Self {
            mode: Mode::Smart, countries: Vec::new(), profiles: Vec::new(),
            fallback_attempts: 5, final_fallback: FinalFallback::Tor,
            ping_interval_seconds: 10, priority: PriorityWeights::default(),
            benchmark: BenchmarkConfig::default(),
            proton_api_base: "https://vpn-api.proton.me".into(),
            proxy_bind: "127.0.0.1:8080".into(),
            tor_executable: "./Tor/tor/tor.exe".into(),
            openvpn_executable: "openvpn.exe".into(),
            wireguard_go_executable: "wireguard-go.exe".into(),
            wg_executable: "wg.exe".into(), stealth_executable: None,
            allow_external_tunnel_commands: false,
        }
    }
}

impl AppConfig {
    pub fn path() -> PathBuf {
        let app_dir = std::env::current_exe().ok()
            .and_then(|p| p.parent().map(PathBuf::from))
            .unwrap_or_else(|| PathBuf::from("."));
        app_dir.join("config").join("config.toml")
    }

    fn legacy_path() -> Option<PathBuf> {
        ProjectDirs::from("com", "Community", "ProtonVpnClient")
            .map(|p| p.config_dir().join("config.toml"))
    }

    pub fn load_or_default() -> Result<Self> {
        let path = Self::path();
        if !path.exists() {
            if let Some(legacy) = Self::legacy_path().filter(|p| p.is_file()) {
                let raw = fs::read_to_string(&legacy)
                    .with_context(|| format!("reading previous config {}", legacy.display()))?;
                let value: Self = toml::from_str(&raw).context("parsing previous config.toml")?;
                value.validate()?;
                value.save()?;
                return Ok(value);
            }
            let value = Self::default();
            value.save()?;
            return Ok(value);
        }
        let raw = fs::read_to_string(&path).with_context(|| format!("reading {}", path.display()))?;
        let value: Self = toml::from_str(&raw).context("parsing config.toml")?;
        value.validate()?;
        Ok(value)
    }

    pub fn save(&self) -> Result<()> {
        self.validate()?;
        let path = Self::path();
        if let Some(parent) = path.parent() { fs::create_dir_all(parent)?; }
        let tmp = path.with_extension("toml.tmp");
        fs::write(&tmp, toml::to_string_pretty(self)?)?;
        if path.exists() { fs::remove_file(&path)?; }
        fs::rename(tmp, path)?;
        Ok(())
    }

    pub fn validate(&self) -> Result<()> {
        if !(1..=20).contains(&self.fallback_attempts) {
            bail!("fallback attempts must be between 1 and 20");
        }
        if !(1..=3600).contains(&self.ping_interval_seconds) {
            bail!("normal ping interval must be between 1 and 3600 seconds");
        }
        if !(1..=3600).contains(&self.benchmark.ping_interval_seconds) {
            bail!("benchmark ping interval must be between 1 and 3600 seconds");
        }
        if self.benchmark.rotate_after_seconds == 0 {
            bail!("benchmark rotation interval must be at least one second");
        }
        let bind: SocketAddr = self.proxy_bind.parse().context("proxy_bind must be an IP:port address")?;
        if !bind.ip().is_loopback() {
            bail!("the proxy must bind to a loopback address; exposing it to the LAN is disabled");
        }
        if self.proton_api_base.trim().is_empty() {
            bail!("Proton API base URL cannot be empty");
        }
        Ok(())
    }
}
