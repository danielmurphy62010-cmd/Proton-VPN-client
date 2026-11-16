# Architecture

## Processes

`proton-vpn-client.exe` is the Rust supervisor. It owns the local HTTP API, local proxy, Tor child process, and the Qt helper. On Windows it puts itself into a Job Object configured with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` before starting children. Child processes inherit the job membership. Force-terminating the Rust supervisor closes its job handle and Windows terminates the job members.

The Qt 6 helper (`proton-vpn-gui.exe`) is kept separate to use native Qt Widgets and Qt WebEngine APIs. It receives a randomly generated local API token and ephemeral port through its environment. The API binds to 127.0.0.1 and checks the token on every route.

## Configuration

The TOML configuration is stored below the current user's application config directory. The UI edits it through `PUT /api/config`. Active process state is separate from the config object. Therefore changing the selected protocol or priority values changes subsequent connection attempts only; it does not kill or mutate a running process.

API session bearer token and UID are memory-only. They are deliberately not serialized to disk.

## Proton server list

The Rust client sends an authenticated request to `/vpn/logicals?SignServer=Server.EntryIP,Server.Label` and decodes `LogicalServers`. The parser flattens physical server entry addresses and per-protocol address fields. The API response model used by the official app contains protocol-specific IPv4 entry values but no port value in those fields. Ports therefore remain optional/null.

## Proxy routes

The explicit HTTP proxy listens on a loopback socket. For Tor routes, outbound TCP streams are opened through SOCKS5 at 127.0.0.1:9050. For VPN/LAN routes, outbound streams use the Windows system route. This is not a transparent proxy and does not install a system-wide interception rule.

Only one system route is supported at a time. To route multiple independent profiles simultaneously, each profile would need its own TUN interface plus per-socket/interface binding or Windows policy routes. A loopback HTTP proxy does not create those interfaces.

## Measurement

The current measurement is a TCP connection to 1.1.1.1:443; it is not ICMP echo. The probe follows the active route (or Tor SOCKS route) and is attributed only to the active profile. JSON retains the latest 200 points, with ping_count capped at 200. A future implementation can replace the probe with ICMP or a multi-interface probe once each profile has a distinct network interface.

## Protocol adapters

OpenVPN and wireguard-go are external child processes. They are not bundled. Driver availability, routes, DNS, kill switch behavior and permissions are host responsibilities. Stealth is not synthesized: Proton's Windows client uses its ProTun TLS backend, which the requested design excludes. An external executable hook is available for a separately supplied compatible implementation.
