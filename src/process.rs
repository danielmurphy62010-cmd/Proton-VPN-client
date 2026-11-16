use anyhow::{bail, Context, Result};
use std::process::Stdio;
use tokio::process::{Child, Command};

pub fn wireguard_interface_name(profile_id: &str) -> String {
    format!("pv{}", profile_id.chars().filter(|c| c.is_ascii_alphanumeric()).take(8).collect::<String>())
}

pub async fn spawn_profile_process(
    protocol: &crate::config::Mode,
    profile: &crate::config::Profile,
    config: &crate::config::AppConfig,
) -> Result<Child> {
    if !config.allow_external_tunnel_commands {
        bail!("external tunnel commands are disabled. Enable them in Settings after installing and configuring the required tunnel driver.");
    }
    let path = profile.config_file.as_ref().context("profile has no imported .ovpn/.conf file")?;
    if !path.is_file() { bail!("profile config file does not exist: {}", path.display()); }

    let mut child = match protocol {
        crate::config::Mode::OpenVpn => {
            let mut c = Command::new(&config.openvpn_executable);
            c.arg("--config").arg(path).arg("--verb").arg("3")
                .stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null())
                .kill_on_drop(true).spawn()
                .with_context(|| format!("starting OpenVPN executable {}", config.openvpn_executable.display()))?
        }
        crate::config::Mode::WireGuard => {
            let interface = wireguard_interface_name(&profile.id);
            let mut c = Command::new(&config.wireguard_go_executable);
            c.arg(&interface).stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null()).kill_on_drop(true);
            let mut child = c.spawn().with_context(|| format!("starting wireguard-go {}", config.wireguard_go_executable.display()))?;
            tokio::time::sleep(std::time::Duration::from_millis(500)).await;
            let status = Command::new(&config.wg_executable)
                .arg("setconf").arg(&interface).arg(path)
                .stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null())
                .status().await
                .with_context(|| format!("running wg setconf using {}", config.wg_executable.display()))?;
            if !status.success() {
                let _ = child.kill().await;
                let _ = child.wait().await;
                bail!("wg setconf failed for interface {interface}; confirm WireGuard and a usable Wintun driver are installed");
            }
            child
        }
        crate::config::Mode::Stealth => {
            let exe = config.stealth_executable.as_ref()
                .context("Stealth has no supported standalone executable configured; the Proton Windows implementation uses its ProTun backend")?;
            let mut c = Command::new(exe);
            c.arg("--config").arg(path).stdin(Stdio::null()).stdout(Stdio::null())
                .stderr(Stdio::null()).kill_on_drop(true);
            c.spawn().with_context(|| format!("starting Stealth backend {}", exe.display()))?
        }
        _ => bail!("this mode does not launch a VPN profile"),
    };
    tokio::time::sleep(std::time::Duration::from_millis(700)).await;
    if let Some(exit) = child.try_wait().context("checking tunnel process startup")? {
        bail!("tunnel process exited during startup with status {exit}");
    }
    Ok(child)
}

pub async fn stop_child(child: &mut Child) {
    let _ = child.kill().await;
    let _ = child.wait().await;
}

#[cfg(windows)]
pub struct JobGuard(windows_sys::Win32::Foundation::HANDLE);

#[cfg(windows)]
impl JobGuard {
    pub fn new() -> Result<Self> {
        use std::mem::{size_of, zeroed};
        use std::ptr::null;
        use windows_sys::Win32::System::JobObjects::{
            AssignProcessToJobObject, CreateJobObjectW, SetInformationJobObject,
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION, JobObjectExtendedLimitInformation,
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
        };
        use windows_sys::Win32::System::Threading::GetCurrentProcess;
        unsafe {
            let job = CreateJobObjectW(null(), null());
            if job.is_null() { bail!("CreateJobObjectW failed: {}", std::io::Error::last_os_error()); }
            let mut info: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = zeroed();
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                &info as *const _ as *const _, size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32) == 0 {
                let err = std::io::Error::last_os_error();
                windows_sys::Win32::Foundation::CloseHandle(job);
                bail!("SetInformationJobObject failed: {err}");
            }
            if AssignProcessToJobObject(job, GetCurrentProcess()) == 0 {
                let err = std::io::Error::last_os_error();
                windows_sys::Win32::Foundation::CloseHandle(job);
                bail!("AssignProcessToJobObject failed; child cleanup guarantee cannot be established: {err}");
            }
            Ok(Self(job))
        }
    }
}
#[cfg(windows)]
impl Drop for JobGuard {
    fn drop(&mut self) { unsafe { windows_sys::Win32::Foundation::CloseHandle(self.0); } }
}

#[cfg(not(windows))]
pub struct JobGuard;
#[cfg(not(windows))]
impl JobGuard { pub fn new() -> Result<Self> { Ok(Self) } }
