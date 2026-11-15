# Proton VPN Client (Rust + Qt 6)

A Windows-first Proton VPN connection manager with a Qt 6 desktop interface, tray-resident operation, Tor lifecycle management, profile selection, health measurements, and a loopback HTTP proxy.

## Important networking limitation

A loopback HTTP proxy is not a replacement for a Windows TUN interface. Without an administrator-installed driver/service, this application cannot transparently route all device traffic through OpenVPN or WireGuard. The unprivileged mode therefore exposes an explicit HTTP proxy at 127.0.0.1:8080 and supports applications configured to use it. Tor can provide SOCKS-based routing. A future privileged adapter can provide full-device routing without changing the GUI or profile model.

## Design goals

- Qt 6 GUI remains hidden until opened from the system tray.
- A background supervisor owns child processes and performs orderly shutdown.
- Configuration is stored locally and all supported policy settings are exposed in the GUI.
- Existing profile sessions are immutable snapshots: changing the selected protocol affects future connections only.
- OpenVPN, WireGuard, and Stealth are represented as distinct providers. Provider availability depends on Proton account/API support and installed executables.
- Tor is supervised continuously and is not terminated merely because another route is selected.
- Normal and benchmark telemetry use separate settings and JSON data.
- Fallback count is bounded from 1 to 20, with a configurable final Tor or direct/LAN route.

## Security notes

- Never commit Proton cookies, credentials, private keys, or generated WireGuard configurations.
- Login should use Proton's supported authentication flow. Do not scrape browser cookies or store passwords in plaintext.
- Proton API endpoints and response schemas are versioned externally; API integration must validate responses and fail closed when a schema changes.
- ICMP ping may be blocked or rate-limited. Measurements must record failures rather than treating them as zero latency.

## Build prerequisites

- Windows 10/11 x64
- Rust stable
- Qt 6 development installation and the matching CMake/Ninja toolchain
- Tor Expert Bundle at `./Tor/tor/tor.exe`
- Optional OpenVPN executable and WireGuard userspace tooling
- Proton API account/session for authenticated features

The initial repository scaffold is being built in small, reviewable stages. Features that require privileged networking or undocumented Proton API behavior are explicitly gated rather than simulated.
