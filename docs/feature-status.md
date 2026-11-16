# Feature status

| Feature | Status | Notes |
|---|---|---|
| Qt GUI and tray | Implemented in source | Card-based layout and embedded Proton sign-in browser. |
| Proton browser authentication | Implemented in source | Captures authenticated Bearer/UID headers, not passwords or cookies. |
| Proton API through Tor | Implemented in source | API requests use Tor SOCKS5H at `127.0.0.1:9050`; no direct fallback. |
| Proton account/server/client configuration | Implemented in source | Account plan, protocol endpoints, ports and Smart Protocol settings fetched from the API. |
| Device certificate provisioning | Implemented in source | Ed25519 client key and certificate requested; key and cert retained in process memory. |
| Explicit HTTP proxy | Implemented in source | Binds to `127.0.0.1:8080`; HTTP absolute-form and HTTPS CONNECT. |
| Proxy via Tor | Implemented in source | Existing SOCKS5 route. |
| Proxy via system route | Implemented in source | Direct connection through the normal Windows network. |
| Proxy via Proton WireGuard | Not yet implemented | Requires BoringTun plus a user-space IP/TCP stack; the old Wintun adapter was removed because it required admin. |
| Proxy via OpenVPN | Not yet implemented | Requires OpenVPN 3 client core or equivalent plus the same user-space packet/stream bridge. |
| Proton Stealth without ProTun | Not yet implemented | Needs a compatible independent transport; generic WireGuard TLS is not Proton Stealth. |
| DNS inside Proton tunnel | Not yet implemented | Must be integrated into the user-space network stack to avoid local DNS leakage. |
| No imported VPN profiles | Not yet complete | Existing legacy profile code remains, but is not the desired final connection workflow. |
| No administrator privileges | Required by design | No driver installation or system-wide route changes in the proxy-only architecture. |

Build success only verifies compilation and packaging. No live Proton VPN tunnel is currently claimed as operational.
