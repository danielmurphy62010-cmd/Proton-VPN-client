# Architecture

## Processes

`proton-vpn-client.exe` is the Rust supervisor. It starts the existing Tor Expert Bundle, owns the local HTTP API and explicit HTTP proxy, starts the Qt helper, and manages tunnel child processes. On Windows it uses a Job Object configured with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` to clean up child processes when the supervisor exits.

The Qt 6 helper (`proton-vpn-gui.exe`) provides the card-based interface and an embedded Proton sign-in browser. It receives a random local API token and ephemeral API port through its environment.

## Tor and Proton API traffic

The supervisor starts Tor from the portable layout:

- `Tor/tor/tor.exe`
- `Tor/data/geoip` and `Tor/data/geoip6` when supplied
- `Tor/state/` for runtime state

The supervisor waits for SOCKS5 on `127.0.0.1:9050` before exposing the GUI. The Rust Proton API client disables system/environment proxy settings and explicitly uses `socks5h://127.0.0.1:9050`, so Proton API requests and their DNS lookup go through Tor. It does not silently fall back to a direct API connection.

After browser sign-in, the Qt WebEngine request interceptor observes HTTPS requests to Proton-owned domains. It forwards only the Bearer token and `x-pm-uid` pair to the authenticated local API. The local API clears any previous VPN provisioning state when a new session is loaded.

## Proton provisioning

An API refresh requests three data groups through Tor:

1. `/vpn/v2/logicals`: logical and physical server records, per-protocol relay addresses, server metadata and load/status.
2. `/vpn/v2`: VPN account entitlement, plan and connection limits.
3. `/vpn/v2/clientconfig`: default OpenVPN/WireGuard port lists and Smart Protocol availability.

The backend expands the protocol entry addresses using Proton's own returned port lists. It does not invent a universal port number.

After refreshing, the backend generates an Ed25519 key pair, builds the public key PEM, requests a client device certificate from `/vpn/v1/certificate`, and derives the related X25519 private key using SHA-512 plus the X25519 scalar clamp. The generated private key and certificate are retained in process memory and are not serialized into the user config or logs.

These API routes and request formats are based on the current official Windows client implementation. They are not a stable, documented third-party VPN SDK contract.

## Tunnel status and remaining integration

The API/server/certificate provisioning data is now available in memory. However, the existing connection path has not yet been rewritten to consume it. It still selects imported local profiles and launches external OpenVPN/WireGuard executables.

A no-import workflow requires a protocol adapter that:
- selects a Proton logical/physical server and a protocol-compatible endpoint;
- builds runtime tunnel settings from the issued device credentials and server metadata;
- starts and supervises a bundled engine;
- configures the Windows virtual adapter, routes, DNS and cleanup policy;
- verifies tunnel readiness before reporting the connection as active.

No native OpenVPN/WireGuard engine, Windows driver installer, kill switch, or Proton-compatible Stealth implementation is currently bundled. Proton's current Windows app uses additional service/native-driver components for those tasks.

## Local proxy

The explicit HTTP proxy listens at `127.0.0.1:8080`. It handles HTTP absolute-form requests and HTTPS CONNECT requests. In Tor mode, proxy TCP streams go through Tor SOCKS5. In other modes, the proxy connects using the Windows system route. It is not a transparent proxy and does not create or enforce a full-system tunnel.

## Measurement

The current measurement is a TCP connect to `1.1.1.1:443`, not ICMP. It follows the active route and is attributed only to the active profile. JSON retains the latest 200 points.
