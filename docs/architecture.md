# Architecture

## Processes

`proton-vpn-client.exe` is the Rust supervisor. It starts portable Tor, owns the local HTTP API and explicit HTTP proxy, launches the Qt UI, and supervises background tasks. The Qt process communicates with the supervisor over a random-token-protected loopback HTTP API.

## Proxy-only operating model

The proxy listener is `127.0.0.1:8080`. It accepts HTTP absolute-form requests and HTTPS CONNECT, then opens an upstream connection according to the selected route. The existing routes are Tor SOCKS5 and the normal system route.

This design intentionally avoids a system-wide Windows tunnel interface. It must not install Wintun/TAP, write Windows default routes, require a privileged service, or request elevation. Only applications explicitly configured to use the local proxy are routed through it.

## Proton API and Tor

Tor is started from the portable directory:

- `Tor/tor/tor.exe`
- optional `Tor/data/geoip` and `Tor/data/geoip6`
- `Tor/state/` for runtime state

All Proton API calls use SOCKS5H at `127.0.0.1:9050`, so API DNS resolution occurs on the Tor side. The API client requests account/plan data, server/relay metadata, client protocol ports and a device certificate. Client private-key material and the certificate are kept in memory.

## VPN-over-proxy work remaining

A raw WireGuard or OpenVPN tunnel carries IP packets; an HTTP proxy expects byte streams. A no-admin implementation therefore needs a user-space IP/TCP stack between them:

1. The HTTP proxy asks the VPN stack to open a virtual TCP connection to the requested destination.
2. The user-space TCP/IP stack turns HTTP proxy stream bytes into IP packets.
3. The WireGuard/OpenVPN engine encrypts and transports those packets to Proton.
4. Decrypted reply packets are fed back into the user-space stack and exposed as a stream to the proxy.
5. DNS queries are sent through that same virtual stack to a tunnel-side resolver.

BoringTun is retained as the WireGuard protocol library, but the previous Wintun interface prototype was removed because it required administrator privileges and changed system routes. The user-space TCP/IP-to-WireGuard bridge has not yet been completed.

OpenVPN requires an embedded OpenVPN 3 client core (or equivalent) and a compatible user-space packet interface. Proton Stealth requires a compatible protocol implementation; it is not safe to substitute generic TLS-wrapped WireGuard and call it Stealth.

## Security boundaries

- API credentials and generated private keys are held in memory.
- Local API and HTTP proxy bind to loopback only.
- No VPN connection should be reported as active until the real protocol handshake and proxy data path are verified.
- No system route or network adapter is modified by the intended proxy-only model.
