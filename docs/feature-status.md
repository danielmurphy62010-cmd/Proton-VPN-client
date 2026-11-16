# Feature status

| Feature | Status | Notes |
|---|---|---|
| Qt 6 tray-only window | Implemented in UI source | Window starts hidden; tray trigger opens it; close hides it. |
| GUI configuration | Implemented for supported settings | Mode, country filters, profile selection, fallback, paths, proxy, weights and benchmark controls. |
| Proton logical server JSON | Implemented, authenticated | Requires API Bearer token and UID. API response returns per-protocol entry IPs. |
| All physical IP/port pairs | Partial | Entry IPs are flattened. API payload inspected does not provide a universal port for every protocol endpoint; port remains null. |
| OpenVPN profile import/launch | Implemented with external dependencies | Requires an installed OpenVPN executable and a preinstalled/usable TUN driver; not tested on this host. |
| WireGuard static config | Partial | wireguard-go plus wg setconf launch is present; an installed Wintun driver and route setup may be required. |
| WireGuard config generation via Proton API | Not implemented | Proton's configuration-generation flow is not exposed as a stable public third-party interface in the sources reviewed. |
| Proton Stealth | External hook only | No native implementation. The Proton Windows app uses its ProTun TLS backend; user asked not to use ProTun. |
| Tor always-on supervision | Implemented in source | Starts/restarts Tor if configured executable exists. |
| Mini-browser through Tor | Implemented in UI source | Qt WebEngine Chromium proxy is set to Tor SOCKS5; site cookies are retained in its own profile. |
| Cookie-to-API authentication | Not implemented | API needs Bearer token + UID. Website cookies are not silently treated as API credentials. |
| Local HTTP proxy | Implemented in source | Loopback-only HTTP proxy; Tor mode uses SOCKS5, otherwise uses the system route. |
| 1–20 simultaneous isolated VPN connections | Not implemented | No-admin Windows cannot establish independent policy-routed tunnel interfaces through a local HTTP proxy. Fallback attempts are sequential. |
| Protocol changes preserve active connection | Implemented | Config updates do not mutate the active child/session; disconnect is required before reconnecting. |
| Process cleanup on force termination | Implemented in source | Windows Job Object kill-on-close. Requires successful assignment at startup. |
| Ping telemetry | Partial | Active route only, TCP connect to 1.1.1.1:443 rather than ICMP. 200 rolling samples and a capped count. |
| Benchmark mode | Partial | Separate interval/weights and random rotation among enabled profiles; measurements are active-route TCP connect. |

The source has not been compiled or executed in this environment because a Rust toolchain and Qt 6 SDK are unavailable here. The build script and Windows CI should be used to verify the actual target environment.
