# Control API (Rust core <-> Qt6 GUI)

The core listens on `http://127.0.0.1:<PVC_CONTROL_PORT>` (default 8081, loopback only).
Every request must carry header `X-Token: <PVC_TOKEN>`; both values are passed to the GUI in
environment variables when the core spawns it. Bodies and responses are JSON.
Errors: HTTP 4xx/5xx with `{"error": "message"}`.

| Method | Path | Body | Response |
|---|---|---|---|
| GET | `/state` | | `{mode, benchmark_enabled, proxy, logged_in, tor:{running,bootstrapped,socks}, connections:[{id,protocol,country,server,state,ping_ms,since}], log_tail:[string]}` |
| GET | `/config` | | the full configuration object |
| PUT | `/config` | full configuration object | `{ok:true}` or 400 `{error}` |
| GET | `/schema` | | `{enums:{<path>:[string]}, ranges:{<path>:[min,max]}, docs:{<path>:string}}` (paths are dotted, arrays written `name[]`) |
| POST | `/mode` | `{mode: "smart"\|"ovpn"\|"wireguard"\|"stealth"\|"tor"\|"lan"}` | `{ok:true}` |
| POST | `/benchmark` | `{enabled: bool}` | `{ok:true}` |
| GET | `/profiles` | | `[{id,protocol,country,city,server,entry_ip,port,transport,tier,features:[string],enabled,connected,stats:{avg_ping_ms,ping_count,successes,failures,connections}}]` |
| GET | `/countries` | | `[string]` (country codes present in the server list) |
| POST | `/connect` | `{id}` | `{ok:true}` |
| POST | `/disconnect` | `{id}` | `{ok:true}` (manual disconnect; the profile is not auto-reconnected) |
| POST | `/servers/refresh` | | `{ok:true}` |
| GET | `/auth` | | `{logged_in: bool}` |
| POST | `/auth/cookies` | `{uid, auth, session_id}` | `{ok:true}` |
| POST | `/auth/logout` | | `{ok:true}` |
| GET | `/stats` | | `{normal:{<id>:{...}}, benchmark:{<id>:{...}}}` |
| POST | `/quit` | | `{ok:true}`, then the core exits and kills every child process |
