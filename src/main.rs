#![cfg_attr(target_os = "windows", windows_subsystem = "windows")]

mod api;
mod config;
mod metrics;
mod process;
mod proxy;
mod runtime;

use anyhow::{Context, Result};
use config::AppConfig;
use process::JobGuard;
use runtime::AppState;
use std::{fs, path::PathBuf};
use tokio::process::Command;
use tracing_subscriber::EnvFilter;

#[tokio::main]
async fn main() -> Result<()> {
    let _job = JobGuard::new().context("creating Windows kill-on-close process job")?;
    let executable = std::env::current_exe().context("locating the portable application directory")?;
    let app_dir = executable.parent().context("application executable has no parent directory")?;
    std::env::set_current_dir(app_dir).context("setting the working directory to the application folder")?;
    fs::create_dir_all("./data/logs")?;
    let log_dir = PathBuf::from("./data/logs");
    fs::create_dir_all(&log_dir)?;
    let log_file = tracing_appender::rolling::never(log_dir, "client.log");
    let (writer, _log_guard) = tracing_appender::non_blocking(log_file);
    tracing_subscriber::fmt().with_writer(writer)
        .with_env_filter(EnvFilter::from_default_env()).init();

    let mut config = AppConfig::load_or_default()?;
    config.tor_executable = resolve_app_path(&config.tor_executable);
    config.openvpn_executable = resolve_app_path(&config.openvpn_executable);
    config.wireguard_go_executable = resolve_app_path(&config.wireguard_go_executable);
    config.wg_executable = resolve_app_path(&config.wg_executable);
    if let Some(path) = config.stealth_executable.as_ref() {
        config.stealth_executable = Some(resolve_app_path(path));
    }
    let proxy_bind = config.proxy_bind.clone();
    let app = AppState::new(config);
    let client_token = uuid::Uuid::new_v4().to_string();

    let (api_handle, quit_rx, api_port) = runtime::serve_api(app.clone(), client_token.clone()).await?;
    let proxy_handle = match runtime::serve_proxy(app.clone(), proxy_bind.clone()).await {
        Ok(handle) => Some(handle),
        Err(error) => {
            tracing::error!(%error, "local proxy could not start");
            None
        }
    };
    let supervisor_handle = tokio::spawn(runtime::run_supervisors(app.clone()));

    let gui_path = std::env::current_exe()?.with_file_name("proton-vpn-gui.exe");
    if !gui_path.is_file() {
        app.stop_all().await;
        bail_gui_missing(&gui_path)?;
    }

    let mut gui = Command::new(&gui_path);
    gui.env("PROTON_CLIENT_API", format!("http://127.0.0.1:{api_port}"))
        .env("PROTON_CLIENT_TOKEN", client_token)
        .env("PROTON_CLIENT_PROXY", proxy_bind)
        .kill_on_drop(true);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        gui.as_std_mut().creation_flags(0x08000000);
    }
    let mut child = gui.spawn().with_context(|| format!("starting Qt GUI {}", gui_path.display()))?;
    tracing::info!("client services started");

    tokio::select! {
        result = child.wait() => {
            match result {
                Ok(status) => tracing::info!(%status, "Qt GUI exited"),
                Err(error) => tracing::warn!(%error, "Qt GUI wait failed"),
            }
        }
        _ = quit_rx => {
            let _ = child.kill().await;
            let _ = child.wait().await;
        }
    }

    app.stop_all().await;
    supervisor_handle.abort();
    api_handle.abort();
    if let Some(handle) = proxy_handle { handle.abort(); }
    tracing::info!("managed processes stopped; exiting");
    Ok(())
}

fn resolve_app_path(path: &std::path::Path) -> PathBuf {
    if path.is_absolute() {
        return path.to_path_buf();
    }
    if path.exists() {
        return path.canonicalize().unwrap_or_else(|_| path.to_path_buf());
    }
    if let Ok(executable) = std::env::current_exe() {
        if let Some(dir) = executable.parent() {
            // Expected portable Tor Expert Bundle layout:
            // <app folder>/Tor/tor/tor.exe, with GeoIP files in Tor/data.
            if path.file_name().is_some_and(|name| name.to_string_lossy().eq_ignore_ascii_case("tor.exe")) {
                let candidate = dir.join("Tor").join("tor").join("tor.exe");
                if candidate.is_file() {
                    return candidate;
                }
            }
            for ancestor in dir.ancestors().take(5) {
                let candidate = ancestor.join(path);
                if candidate.exists() {
                    return candidate;
                }
            }
        }
    }
    path.to_path_buf()
}

fn bail_gui_missing(path: &std::path::Path) -> Result<()> {
    anyhow::bail!("Qt GUI executable is missing: {}. Run build.ps1 to build the Qt 6 tray application.", path.display())
}
