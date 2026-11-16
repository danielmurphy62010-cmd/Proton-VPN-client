# Proton VPN Client

Rust networking supervisor and Qt 6 desktop interface. The intended model is an explicit local HTTP proxy, not a system-wide Windows VPN adapter.

## Intended operating model

- Local explicit HTTP proxy: `127.0.0.1:8080`.
- Applications are configured individually to use that proxy. Other applications continue using their normal Windows connection.
- No Wintun/TAP adapter, default-route replacement, Windows service installation, or administrator privileges are required by the proxy-only design.
- Proton API requests are forced through the portable Tor SOCKS5 service at `127.0.0.1:9050`, including remote DNS resolution.
- Proton account, server, client-port and device-certificate metadata are fetched from Proton after browser authentication. Session and private-key material are held in memory.

## Current implementation status

The local HTTP proxy currently supports HTTP absolute-form requests and HTTPS CONNECT. At present it can route those connections through Tor or the normal system route. The Proton API provisioning layer can fetch logical servers, protocol endpoints, port settings, and a device certificate, but those credentials are **not yet wired into a proxy-only WireGuard/OpenVPN data path**.

WireGuard UDP previously had a Wintun-backed prototype. That was removed because it required administrator privileges and changed system routes, which conflicts with the requested local-proxy-only design. BoringTun remains as a protocol dependency for the user-space tunnel implementation.

There is no complete OpenVPN or Stealth implementation in this repository yet. Proton Stealth is not equivalent to generic WireGuard TLS, and this client will not label an unrelated transport as Proton Stealth. The old imported-profile/external-executable path is legacy code and is not the intended no-profile connection flow.

## Build on Windows

Install Rust stable with the MSVC toolchain, CMake and Qt 6.5+ with Widgets, Network, WebEngineCore and WebEngineWidgets. Set `QT_ROOT` and run:

```powershell
./build.ps1
```

This builds `proton-vpn-client.exe` and `proton-vpn-gui.exe`. Keep the Tor Expert Bundle beside the executables using the existing layout:

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

The application does not download or extract Tor. Tor is only used for Proton API traffic and the configured Tor fallback.

## API provisioning

After sign-in, the backend calls Proton's current Windows-client API endpoints through Tor:

- `GET /vpn/v2` for account and plan details.
- `GET /vpn/v2/clientconfig` for protocol availability and default ports.
- `GET /vpn/v2/logicals` for logical and physical servers plus protocol-specific relay addresses.
- `POST /vpn/v1/certificate` for device certificate provisioning.

These are reverse-engineered client endpoints, not a stable third-party SDK. Proton can change them.

## Proxy-only tunnel architecture still required

To deliver the requested no-admin WireGuard/OpenVPN/Stealth behavior, each proxy TCP connection must be connected to a user-space IP stack. That stack must create TCP/IP packets, feed them into the selected VPN protocols, decrypt responses and bridge the virtual TCP stream back to the HTTP proxy. DNS queries must also be resolved inside that virtual stack so domain lookups do not leak to the local network.

A Wintun/TAP interface is deliberately not part of that design.
