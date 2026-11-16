# Feature status

| Feature | Status | Notes |
|---|---|---|
| Card-based Qt GUI | Implemented in source | Connections, Analytics, Profiles & Servers, Config; main window is visible at startup. |
| Non-modal Proton sign-in window | Implemented in source | Can be used without blocking the main client. |
| Browser login detection | Implemented in source | Captures Proton Bearer and UID headers from HTTPS Proton-domain requests; does not read password or cookies. |
| Proton account/plan API | Implemented in source | `GET /vpn/v2`; routed through Tor. |
| Proton logical servers | Implemented in source | `GET /vpn/v2/logicals`; includes protocol-specific relay addresses and metadata. |
| Proton client protocol/port settings | Implemented in source | `GET /vpn/v2/clientconfig`; server-advertised ports are used rather than guessed. |
| Proton device certificate request | Implemented in source | Generates Ed25519 keypair and calls `POST /vpn/v1/certificate`; issued certificate and key remain memory-only. |
| Ed25519 to X25519 client key derivation | Implemented in source | Mirrors the conversion used by Proton's Windows client. |
| Proton API over Tor | Implemented in source | Reqwest client uses `socks5h://127.0.0.1:9050`; startup and API refresh fail closed if Tor is unavailable. |
| API-driven connection plan/configuration | Not yet implemented | API snapshot and device credentials are fetched, but they are not yet used to create a live tunnel. |
| No imported VPN profiles | Not yet implemented | Current connection path still depends on user-imported `.ovpn` / `.conf` profiles. |
| Bundled native OpenVPN engine | Not implemented | Existing process adapter still expects an external OpenVPN executable. |
| Native WireGuard engine | Not implemented | Existing process adapter still expects wireguard-go and wg executables and does not bundle them. |
| Proton Stealth without ProTun | Not implemented | No compatible alternative has been implemented. Do not label generic WireGuard TLS as Proton Stealth. |
| Local HTTP CONNECT proxy | Implemented in source | Loopback only at `127.0.0.1:8080`; explicit proxy, not transparent system-wide routing. |
| Tor supervision | Implemented in source | Starts the existing portable Tor Expert Bundle before launching the client. |
| Multiple simultaneous independent VPN routes | Not implemented | Existing backend only supports one active system route; no per-interface routing engine. |
| Ping telemetry | Partial | TCP connect to `1.1.1.1:443`, not ICMP; attributed to active profile only. |
| Benchmark mode | Partial | Separate interval/weights and profile rotation remain tied to imported profiles. |

The API/provisioning code is in source but must still pass the latest Windows CI build and be tested against a real authenticated Proton account. A green compile does not validate live API responses or a working VPN tunnel.
