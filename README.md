# Proton VPN Client

Rust networking supervisor with a Qt 6 Widgets tray interface for Windows. The Qt GUI is a separate C++ front end; API access, Tor supervision, local proxying, credential handling, metrics and connection policy are implemented in Rust.

## Current implementation

- Qt GUI uses a card-based dark layout with Connections, Analytics, Profiles & Servers, and Config pages. The main window opens at startup; closing it hides it in the system tray.
- The embedded Proton sign-in browser is modeless. A request interceptor watches HTTPS requests to Proton-owned domains for the authenticated `Authorization: Bearer` and `x-pm-uid` headers. Once both are seen, the local Rust API accepts them and the browser closes.
- The Rust supervisor starts the bundled Tor process before launching the GUI. If Tor is unavailable, startup fails closed instead of making Proton API requests directly.
- Proton API requests use an explicit SOCKS5H proxy at `127.0.0.1:9050`. DNS for those API requests is resolved on the Tor side.
- An authenticated refresh fetches Proton VPN account/plan information (`/vpn/v2`), official client protocol/port configuration (`/vpn/v2/clientconfig`), and logical server metadata (`/vpn/v2/logicals`). Protocol endpoints are expanded using Proton's returned port lists; ports are not guessed.
- After refreshing, the client generates an Ed25519 key pair, requests a device certificate from `/vpn/v1/certificate`, derives the corresponding X25519 client key, and retains the certificate/key material in process memory. The private key is not serialized to the config file or logs.
- The current tunnel connection path is still profile-based and still calls external OpenVPN/WireGuard executables. API-driven credential and endpoint provisioning is now present, but it is not yet wired to native tunnel engines.
- The local HTTP CONNECT / plain HTTP proxy binds to loopback at `127.0.0.1:8080`. Tor mode sends proxy traffic through Tor; non-Tor modes use the active Windows system route. This is an explicit proxy, not a transparent system-wide VPN interface.
- Normal telemetry stores rolling 200-sample JSON files. Benchmark mode retains separate measurement intervals/weights.

## Build (Windows)

Install Rust stable with the MSVC toolchain, CMake, and Qt 6.5+ with Widgets, Network, WebEngineCore and WebEngineWidgets. Set `QT_ROOT` to the Qt installation prefix, then run:

```powershell
./build.ps1
```

This produces `target/release/proton-vpn-client.exe` and `target/release/proton-vpn-gui.exe`. Keep the existing Tor Expert Bundle layout extracted beside `proton-vpn-client.exe`:

```
Proton-VPN/
├── proton-vpn-client.exe
├── proton-vpn-gui.exe
└── Tor/
    ├── tor/
    │   └── tor.exe
    ├── data/
    │   ├── geoip
    │   └── geoip6
    └── state/
```

The client assumes Tor is already present there; it does not download or extract Tor. Tor state is stored in `Tor/state/`. The application config remains in `config/config.toml`; logs, metrics and browser profile data are stored below the portable application folder.

## Proton API authentication and provisioning

The browser sign-in flow obtains the same Bearer-token and UID pair used by Proton's authenticated API calls. The browser does not read the password or extract credentials from cookies. The session token, UID, generated Ed25519 private key, derived X25519 key and issued device certificate are memory-only. A new sign-in and certificate request are required after the process exits or the certificate/session expires.

The API refresh is routed through Tor and currently requests:

- `GET /vpn/v2/logicals` for logical/physical servers and protocol-specific relay addresses;
- `GET /vpn/v2` for VPN plan and entitlement data;
- `GET /vpn/v2/clientconfig` for official default protocol ports and Smart Protocol availability;
- `POST /vpn/v1/certificate` for an issued device certificate.

These are endpoints used by Proton's official Windows client, not a documented stable third-party VPN integration contract. Proton can change them.

## Tunnel implementation status

- The current code still imports `.ovpn` and WireGuard `.conf` files and attempts to launch external executables. Those are not yet replaced by bundled native engines.
- API-supplied server data and device credentials are now available in memory, but no API-driven tunnel configuration is connected to a native WireGuard/OpenVPN engine yet.
- A genuine full Windows VPN route requires a TUN/TAP/WireGuard adapter, routing and DNS management, and elevated Windows service/driver setup. A loopback HTTP proxy alone does not provide that.
- Proton's current Windows codebase uses separate native tunnel/service components and OpenVPN resources. The repository has not copied or bundled those components.
- Proton's current Stealth path depends on its Proton protocol engine. This project does not implement that engine; WireGuard TLS, if added independently, must not be mislabeled as Proton Stealth.

## Security

- Do not commit session tokens, private keys, device certificates, browser cookies or imported VPN profiles.
- API credentials and generated tunnel key material are memory-only in this version.
- The local API uses a random per-run token and binds to `127.0.0.1`.
- The HTTP proxy binds to loopback only.
- Proton API requests fail closed if Tor SOCKS5 is unavailable.
- The current executable process cleanup guarantee depends on Windows allowing the client to assign itself to a Job Object.

See [docs/feature-status.md](docs/feature-status.md) and [docs/architecture.md](docs/architecture.md).
