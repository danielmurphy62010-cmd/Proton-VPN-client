# pvc-gui

System-tray GUI for the custom Proton VPN client. It is a pure client of the Rust core's
HTTP/JSON control API (`../docs/control-api.md`); it keeps no state of its own besides the
persistent web profile used for the Proton login.

Status: written without access to Qt or a compiler. It has NOT been built or run. Expect to
fix a few compile errors on the first build.

## Build

Requirements: Qt 6 (Widgets, Network, WebEngineWidgets), CMake 3.16+, a C++17 compiler.

    cmake -S gui -B gui/build -DCMAKE_PREFIX_PATH=<path to Qt6>
    cmake --build gui/build --config Release

The executable is `pvc-gui` (a GUI-subsystem application on Windows, so no console window).

## Environment (set by the core when it launches the GUI)

| Variable | Meaning | Default |
|---|---|---|
| `PVC_CONTROL_PORT` | control API port on 127.0.0.1 | 8081 |
| `PVC_TOKEN` | sent as `X-Token` on every request | empty |
| `PVC_TOR_SOCKS_PORT` | Tor SOCKS port used by the embedded browser | 9050 |

## Behaviour

- Tray-only: the tray icon always exists, the window is not shown at startup. Left click toggles
  the window, closing the window only hides it. Tray menu: Open, Mode (Smart, OVPN, WireGuard,
  Stealth, Tor, LAN), Quit (POST `/quit`, then the GUI exits). Icon colour: green when at least
  one connection is in state `connected`, amber otherwise, red while the core is unreachable.
  If the platform reports no system tray at all, the window is shown at startup as a fallback.
- Polling: `GET /state` every 1 s while the window is visible, every 3 s while hidden (1 s while
  the core is failing). If `/state` fails at the network level continuously for 10 s (and at
  least 3 attempts), the GUI quits.
- Tabs: Status, Profiles (with the Countries panel), Priority, Settings (generic editor for the
  whole config), Stats.
- Login: "Log in to Proton" opens a `QWebEngineView` on `proton.login_url` (default
  `https://account.protonvpn.com/login`). `main()` sets `QTWEBENGINE_CHROMIUM_FLAGS` to
  `--proxy-server=socks5://127.0.0.1:<tor port>` before `QApplication` exists, so all Chromium
  traffic goes through Tor. The control API client uses `QNetworkProxy::NoProxy`. The web profile
  is persistent under `<AppDataLocation>/webprofile`. When an `AUTH-<uid>` cookie and a
  `Session-Id` cookie have both been seen, `{uid, auth, session_id}` is POSTed to
  `/auth/cookies` and the dialog closes.

## Files

- `src/main.cpp` - environment, Chromium proxy flag, application setup.
- `src/ApiClient.*` - async `QNetworkAccessManager` wrapper, serialized read-modify-write of the config.
- `src/MainWindow.*` - tray icon, tray menu, polling, tab wiring.
- `src/StatusTab.*`, `src/ProfilesTab.*`, `src/PriorityTab.*`, `src/StatsTab.*` - the tabs.
- `src/ConfigEditor.*` - generic JSON tree editor (Settings tab).
- `src/LoginDialog.*` - Tor-routed Proton login browser.
- `src/Common.h`, `src/Widgets.h` - JSON path helpers, spin box / combo box without accidental wheel changes.

## Assumptions and known weak spots

- The API does not define the strings of a connection's `state`; the tray turns green when a
  connection's state equals `connected` (case-insensitive).
- Profile enable/disable and the country list edit the config through read-modify-write
  (`GET /config`, change one path, `PUT /config`). For the benchmark config the "enabled" column
  is derived from the chosen include/exclude list, because `GET /profiles` only reports the
  effective state for the normal config.
- Settings tab: numbers are shown in a spin box that accepts decimals; whether a field is an
  integer is not known to the GUI, so typing a fraction into an integer field is rejected by the
  core with a 400 (the error text is shown). Values that were not touched are sent back unchanged.
  Object keys cannot be added or removed (only existing fields are editable). New items in a list
  are copies of the last item; for an empty list the GUI asks for the item type, and an empty
  list of objects can only get empty objects. Key order is alphabetical (Qt JSON).
- Applying the Settings tab PUTs the whole object that was loaded, so it overwrites changes made
  elsewhere after loading. The tab reloads by itself when nothing is edited and warns when the
  config changed behind unsaved edits.
- Login: the browser profile keeps cookies between runs. A session that was already sent is not
  sent again automatically (so stale cookies cannot loop); use "Retry sending session" or
  "Clear browser cookies" in the dialog. Logout from the Status tab also clears the browser
  cookies. Web storage (local storage) is not cleared. If Proton sets the `AUTH-<uid>` cookie before
  the login is really complete, the session would be sent too early.
- WebRTC is restricted to public interfaces, but nothing beyond the SOCKS proxy flag guarantees
  that every possible Chromium network feature goes through Tor; review this before relying on it.
- Closing/quitting: `QApplication::quit()` is preceded by a flag so the always-ignoring
  `closeEvent` cannot block the exit.
