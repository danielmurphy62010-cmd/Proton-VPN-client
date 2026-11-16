# Proton VPN Client

Rust networking supervisor with a Qt 6 Widgets tray interface for Windows. The Qt GUI is a small C++ front end; configuration, API requests, proxy routing, process supervision, telemetry and connection policy are implemented in Rust.

## Current implementation

- The Qt process starts hidden with a system-tray icon. A single tray click opens the main window. Closing the window hides it.
- The Rust parent owns the Qt process and tunnel/Tor child processes. On Windows it assigns itself to a Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`; force termination of the parent closes the job and terminates its members.
- The UI exposes mode, countries, imported profiles, fallback policy, normal and benchmark intervals, paths, proxy binding and all priority weights.
- The API server-list client calls Proton's authenticated logical-server endpoint and flattens protocol entry IPs from the returned JSON.
- Static `.ovpn` and WireGuard `.conf` profile imports are supported. OpenVPN and wireguard-go can be launched when explicitly enabled and required drivers are already installed.
- Tor is supervised and restarted when the executable is present. The local HTTP CONNECT / plain HTTP proxy binds only to loopback and uses Tor SOCKS5 in Tor fallback/active Tor mode.
- Normal telemetry stores rolling 200-sample JSON files and caps the displayed ping counter at 200. Benchmark mode uses a separate interval/weight set and rotates randomly among configured profiles.

## Build (Windows)

Install Rust stable with the MSVC toolchain, CMake, and Qt 6.5+ with Widgets, Network, and WebEngineWidgets. Set `QT_ROOT` to the Qt installation prefix, then run:

```powershell
./build.ps1
```

This produces `target/release/proton-vpn-client.exe` and `target/release/proton-vpn-gui.exe`. The build is portable. Keep the existing Tor Expert Bundle layout extracted beside `proton-vpn-client.exe`: `Tor/tor/tor.exe`, `Tor/data/geoip`, and `Tor/data/geoip6` (when supplied by the bundle). The client assumes Tor is already installed in that folder; it does not download, extract, or install Tor. Tor runtime state is stored in `Tor/state/`. Application configuration is stored in `config/config.toml`, and application logs, metrics, and browser profile data are stored under the portable application folder. Existing per-user config is copied into the portable config folder on first launch when one exists.

## Proton API authentication

The application uses `https://vpn-api.proton.me/vpn/logicals?SignServer=Server.EntryIP,Server.Label` and the Proton API's `Authorization: Bearer` plus `x-pm-uid` headers. You must provide a valid API access token and UID in the Login / API tab. They are held in memory only and never written to the config file or logs.

The embedded browser opens Proton sign-in through Tor and retains its own website cookies using a persistent Qt WebEngine profile. **A browser cookie is not automatically converted into the API Bearer token and UID.** Proton does not publish a supported browser-cookie-to-API-token exchange for third-party clients, so the UI deliberately does not scrape or misrepresent the website cookie as an API session.

The API logical-server response exposes per-protocol entry IPs but not a universal port field for every entry. This client leaves the port as null instead of inventing one; port/config values must come from a valid protocol configuration.

## Important networking limitations

- An HTTP proxy is not a TUN interface. It only carries traffic from applications explicitly configured to use `127.0.0.1:8080`; it does not transparently route all Windows traffic.
- Windows OpenVPN/WireGuard tunnel adapters and routing may require a preinstalled driver and administrator-authorized setup. The application does not attempt to install drivers or services without elevation.
- This build supports one active system route at a time. A 1–20 fallback limit is a sequential profile-attempt budget, not 1–20 independently isolated simultaneous tunnels. Independent concurrent VPN routes require separate TUN interfaces and Windows policy routing, which are not available in a no-admin configuration.
- Per-profile latency is measured for the active route only. It is a TCP connect measurement to `1.1.1.1:443`, not ICMP ping. It is not valid to attribute the same machine-wide route measurement to every inactive profile.
- Proton Stealth is not a generic OpenVPN/WireGuard CLI mode. The official Windows client maps Stealth to its proprietary ProTun TLS backend. This repository only permits a user-configured external Stealth executable; it does not implement or redistribute ProTun.
- Tor mode and LAN mode are usable without a VPN adapter. In LAN mode, proxy requests leave over the ordinary Windows route.

## Security

- Do not commit `.ovpn`, WireGuard private keys, session tokens, cookies or credentials.
- API tokens and UIDs are memory-only in this version.
- The local API uses a random per-run token and binds to 127.0.0.1 only.
- The HTTP proxy binds to loopback only.
- The process cleanup guarantee depends on Windows allowing the client to assign itself to a Job Object. Startup fails if it cannot establish this guarantee.

See [docs/feature-status.md](docs/feature-status.md) for feature-by-feature completion and [docs/architecture.md](docs/architecture.md) for process and routing details.
