# Web Server, REST API & Telemetry Component (C++20)

Embedded asynchronous Web communication and user interface engine for the **BGA Reflow Controller**, running natively on **ESP-IDF v6.0.2** and **FreeRTOS**.

This component manages the HTTP 1.1 web server, WebSocket live telemetry broadcast engine (2 Hz / 500 ms), REST API endpoints, Zero-Copy ROM-embedded static asset delivery, captive portal DNS redirection, SoftAP Wi-Fi provisioning with NVS credential persistence, and seamless 1-Click browser OTA firmware updates.

---

## 1. Overview & Architecture

The `web` component runs on **CPU Core 0** (`web_task`, Priority 5) to completely decouple network I/O, JSON serialization, and Wi-Fi stack operations from the time-critical control loop (`control_task`, 5 Hz) and safety supervisor (`safety_task`, 20 Hz) running on **Core 1**.

```
               +-----------------------------------------------------------+
               |                  ESP32-S3 Firmware Core 0                  |
               +-----------------------------------------------------------+
                                             |
                   +-------------------------+-------------------------+
                   |                                                   |
                   v                                                   v
          [ web::WifiManager ]                               [ esp_http_server ]
        - SoftAP (WPA2/WPA3 Mixed)                         - Max 10 Sockets (Protected WS)
        - Captive DNS (UDP 53)                             - Wildcard URI Routing
        - mDNS (http://reflow.local)                       - Header Buffer: 2048 Bytes
                   |                                                   |
                   +-------------------------+-------------------------+
                                             |
                                             v
                      +---------------------------------------------+
                      |               web::WebServer                |
                      +---------------------------------------------+
                             |               |               |
              +--------------+               |               +--------------+
              |                              |                              |
              v                              v                              v
   [ StaticFileServer ]              [ RestApi Handler ]          [ WebSocketHandler ]
  - Zero-Copy ROM Flash             - Full JSON CRUD             - Async Broadcast (2 Hz)
  - Embedded GZIP Assets            - Atomic Cross-Core Queue    - Delta Log Streaming
  - Captive Portal Probes           - Live System POST Report    - Ring Buffer (30 lines)
              |                              |                              |
              v                              v                              v
       (Browser Client)               (Core 1 Queue)                 (Browser Client)
```

### Cross-Core Concurrency & Isolation
* **Asynchronous FSM Command Queue (`fsmCmdPost`):** The REST API running on Core 0 never mutates FSM state directly. All commands (`PREHEAT`, `REFLOW`, `STOP`, `SKIP_STEP`, `RESET_FAULT`, `AUTOTUNE_START`, `RELOAD_SETTINGS`) are posted into a lock-free FreeRTOS queue (`g_fsmCmdQueue`) and safely drained by Core 1.
* **Double-Buffered Telemetry Consumption:** Telemetry frames broadcasted over WebSocket are read from a lock-free atomic snapshot mailbox (`AppController::getSensorSnapshot()`), eliminating mutex contention between Core 0 and Core 1.
* **Socket Health & Connection Isolation:** The HTTP daemon is configured with LRU socket purging (`lru_purge_enable = true`) to reclaim stale client connections when socket pool limits (10 sockets) are approached. Stale client connections are recycled via 2-second timeouts (`recv_wait_timeout = 2`, `send_wait_timeout = 2`, `so_linger = 0`) and TCP backlog buffering (`backlog_conn = 8`). All one-shot static file and REST endpoints emit `Connection: close` headers to immediately release client sockets post-transfer, preserving permanent socket slots exclusively for the WebSocket telemetry stream.

---

## 2. Component Modules

| Module | Source Files | Description |
| :--- | :--- | :--- |
| **`WebServer`** | `web_server.hpp`<br>`web_server.cpp` | Central coordinator for HTTP daemon lifecycle, route dispatching, and UART log hooking. |
| **`RestApi`** | `rest_api.hpp`<br>`rest_api.cpp` | REST API Facade coordinating route registration and static system context dependency injection. |
| **`ControlRoutes`** | `control_routes.hpp`<br>`control_routes.cpp` | Process control endpoints (`/api/control`, `/api/overrides`) with atomic Core 1 command dispatch. |
| **`ProfileRoutes`** | `profile_routes.hpp`<br>`profile_routes.cpp` | Profile management CRUD endpoints (`/api/profiles`, `/api/profile`) with LittleFS persistence. |
| **`SystemRoutes`** | `system_routes.hpp`<br>`system_routes.cpp` | System status, settings, PID library, telemetry history, theme, and Wi-Fi security endpoints. |
| **`OtaRoutes`** | `ota_routes.hpp`<br>`ota_routes.cpp` | 1-Click browser OTA firmware streaming update handler (`/api/ota`) with SHA256 verification. |
| **`ApiContext`** | `api_context.hpp`<br>`api_context.cpp` | Shared singleton state context, body parsing, and standardized JSON response helpers. |
| **`WebSocketHandler`** | `ws_handler.hpp`<br>`ws_handler.cpp` | Real-time 2 Hz (500 ms) telemetry distributor with dead socket reclamation and delta log synchronization. |
| **`StaticFileServer`** | `static_file_server.hpp`<br>`static_file_server.cpp` | Zero-copy GZIP static file server serving ROM-embedded Web UI assets with OS captive portal probes. |
| **`WifiManager`** | `wifi_manager.hpp`<br>`wifi_manager.cpp` | Wi-Fi SoftAP manager, NVS security storage, mDNS responder (`reflow.local`), and captive DNS server. |
| **`FsmCommandQueue`**| `fsm_command_queue.hpp` | FreeRTOS command queue bridge guaranteeing race-free communication between Core 0 and Core 1. |

---

## 3. REST API Specification

### System Telemetry & Configuration
* **`GET /api/status`**: Complete diagnostic snapshot including active FSM state, live temperatures, FreeRTOS stack high-watermarks (`safety`, `burstfire`, `control`, `input`, `web`), heap memory headroom, boot POST diagnostics, LittleFS storage wear-leveling metrics, and compile-time validation limits.
* **`GET /api/settings`**: Returns current machine settings (thermal limits, watchdog windows, PID gains, sensor calibration offsets).
* **`POST /api/settings`**: Validates and persists new machine settings to LittleFS, then dispatches `FsmCommandType::RELOAD_SETTINGS` to Core 1, updating live watchdog, thermal limits, and actuator configs **immediately in RAM without requiring an MCU reboot**. (Modification is safely blocked during active heating cycles).
* **`GET /api/theme`** / **`POST /api/theme`**: Convenience wrappers for user UI theme preference (`dark` / `light`).

### Profile Management
* **`GET /api/profiles`**: Returns array of all stored reflow profiles and active profile selection.
* **`GET /api/profiles/<filename>`** or `GET /api/profile?name=<filename>`: Fetches specific profile JSON definition.
* **`POST /api/profiles`**: Validates and saves profile JSON. Protected against overwriting the factory profile (`factory-profile.json`).
* **`DELETE /api/profiles/<filename>`**: Removes profile from LittleFS. Rejects deletion of factory profile.

### Process Control & Overrides
* **`POST /api/control`**: Dispatches process commands to Core 1 queue:
  * `{"action":"start","profile":"..."}` $\rightarrow$ `PREHEAT`
  * `{"action":"reflow"}` $\rightarrow$ `REFLOW` (strictly requires `PREHEAT` state with `preheatDone=true`; otherwise rejected with `403 FORBIDDEN`)
  * `{"action":"stop"}` $\rightarrow$ `STOP` (mapped automatically to `RESET_FAULT` if system is currently in `FAULT` state)
  * `{"action":"skip"}` $\rightarrow$ `SKIP_STEP`
  * `{"action":"resetFault"}` $\rightarrow$ `RESET_FAULT`
  * `{"action":"autotune","channel":"top","targetTemp":150}` $\rightarrow$ `AUTOTUNE_START`
* **`POST /api/overrides`**: Toggles manual cooling fan or work lamp overrides via `AppController`.

### Tuning & Security
* **`GET /api/pidlibrary`** / **`POST /api/pidlibrary`**: CRUD access to multi-point scheduled PID tuning curves (`pid_library.json`). On save, curves are **applied immediately to RAM** if FSM is in `IDLE`/`DONE`/`FAULT`; the RAM update is deferred until active reflow completion if heating is active (bumpless transfer safety).
* **`GET /api/history`**: Retrieves execution telemetry run logs.
* **`GET /api/security/status`**: Returns SoftAP SSID and whether default password was changed.
* **`POST /api/security/wifi`**: Updates SoftAP WPA passphrase in NVS (8–63 chars).
* **`POST /api/backup/complete`**: Marks instance backup state (`s_backupTaken = true`).

### 1-Click Browser OTA Update
* **`POST /api/ota`**: Streams new firmware binary chunk-by-chunk directly into the next OTA partition (`esp_ota_begin`, `esp_ota_write`, `esp_ota_end`).
  * Validates ESP32 image magic byte (`0xE9`) on the first chunk.
  * Validates image SHA256 digest on completion.
  * Activates new boot partition and schedules an asynchronous MCU reboot after 1.5 seconds.

---

## 4. WebSocket Telemetry Protocol (`/ws`)

Clients connect via WebSocket to `/ws` to receive real-time telemetry frames at 2 Hz (500 ms). Up to **4 concurrent WebSocket clients (`MAX_WS_CLIENTS = 4`)** are supported in firmware.

### Client Concurrency Architecture & Sizing Rationale
The ESP32-S3 HTTP daemon operates with a total pool of **10 sockets** (`MAX_HTTP_SOCKETS = 10`):
* **Recommended Operator Setup (Max. 2 Active Devices):**
  For optimal 2.4 GHz RF airtime and lowest telemetry jitter, operating with at most **2 active physical devices** (e.g. workbench PC monitor + mobile phone/tablet handheld) is strongly recommended. This prevents Wi-Fi channel contention and keeps round-trip latency under 5 ms.
* **Firmware Capacity (4 WebSocket Slots):**
  The firmware allocates `MAX_WS_CLIENTS = 4` slots to effortlessly accommodate:
  * **2 Primary Dashboards:** PC workstation + mobile handheld.
  * **Multi-Tab Workflows:** PC operator opening a second tab (e.g. live chart + profile editor).
  * **Seamless Reconnect Buffer:** Absorbs browser page reloads ($F5$) without bumping active sessions while old TCP sockets finish closing.
  * **6 Dedicated HTTP Sockets:** Guarantees 60% of the socket pool remains permanently open for instant REST commands, profile uploads, and emergency `STOP` aborts.

### Network Stack & Low-Level Buffer Optimization
To eliminate transient socket disconnects (`error in send : 119` / `EINPROGRESS`) caused by 2.4 GHz RF collisions or mobile power-save sleep states (802.11 DTIM beacons), the low-level network stack is finely tuned:

| Parameter | Configuration | Value | Architectural Rationale |
| :--- | :--- | :--- | :--- |
| **TCP Send Buffer** | `CONFIG_LWIP_TCP_SND_BUF_DEFAULT` | `8640 B` ($6 \times \text{MSS}$) | Buffers 5–8 telemetry frames (or 3 full frames with 20 CLI logs) during brief Wi-Fi jitter without stalling the TCP socket. Prevents buffer overflow while avoiding obsolete real-time telemetry accumulation. |
| **TCP Window Size** | `CONFIG_LWIP_TCP_WND_DEFAULT` | `8640 B` ($6 \times \text{MSS}$) | Matches receive window to ensure smooth, unthrottled bidirectional REST and backup transfers. |
| **Wi-Fi TX Buffers** | `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM` | `48` | Provides 50% more internal SRAM queue space (increased from 32) for 802.11 Block-ACK retransmissions during RF interference. |
| **Server Send Timeout** | `config.send_wait_timeout` (`web_server.cpp`) | `2.0 s` | Accommodates mobile phone screen-wake radio delays (802.11 DTIM) while preventing stalled clients from blocking the web server daemon. |
| **Failure Tolerance** | `MAX_CONSECUTIVE_WS_FAILURES` | `5` cycles (2.5 s) | Transient frame drops no longer terminate the session immediately; the socket is only severed if the client remains unresponsive for 5 consecutive 500 ms cycles. |
| **Headless Fast Early-Exit** | `broadcast()` (`ws_handler.cpp`) | `0` clients $\rightarrow$ early return | When no WebSocket clients are active, JSON serialization and string building are completely bypassed, eliminating Core 0 CPU and heap usage. |
| **Single Source of Truth (SSOT)** | `httpd_get_client_list` (`ws_handler.cpp`) | Direct daemon polling | Dispatches frames directly to all active sockets confirmed as `HTTPD_WS_CLIENT_WEBSOCKET` by the ESP-IDF HTTP daemon, eliminating state desynchronization. |

### Long-Term Telemetry History & Memory Safety
* **3-Hour Continuous History:** The controller records up to **10,800 data points** (`MAX_HISTORY_POINTS`, 1 sample/sec) containing 4 simultaneous curves (Top Actual, Bottom Actual, Top Setpoint, Bottom Setpoint).
* **Zero SRAM Exhaustion & PSRAM Pre-Allocation:** The ~216 KB 3-hour history buffer is pre-allocated at boot via `_history.reserve(MAX_HISTORY_POINTS)` directly into the **8 MB Octal PSRAM** (as 216 KB > 64 KB `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`). This eliminates heap fragmentation, guarantees $O(1)$ zero-copy point recording during reflow, and keeps internal SRAM 100% available for Wi-Fi DMA.
* **Chunked Streaming (`GET /api/history`):** The REST endpoint streams historical curves in small 256-byte chunks (`httpd_resp_send_chunk`), requiring less than 2 KB heap during transmission.

### JSON Payload Structure
```json
{
  "stateEnum": 3,
  "state": "REFLOW",
  "preheatDone": true,
  "topTemp": 218.4,
  "bottomTemp": 165.2,
  "topSensorOk": true,
  "bottomSensorOk": true,
  "topSet": 220.0,
  "bottomSet": 165.0,
  "topPower": 78.5,
  "bottomPower": 32.0,
  "elapsed": 142,
  "tal": 18,
  "fan": false,
  "lamp": true,
  "topStep": 2,
  "bottomStep": 2,
  "topSettling": false,
  "bottomSettling": false,
  "topHolding": true,
  "bottomHolding": true,
  "topSettleRemain": 0,
  "bottomSettleRemain": 0,
  "topHoldRemain": 12,
  "bottomHoldRemain": 12,
  "profileFile": "SAC305_LeadFree.json",
  "stepMarkers": [
    {"timeS": 60, "stepIndex": 1, "targetTemp": 150.0, "isTop": false}
  ],
  "topPid": {"kp": 12.5, "ki": 0.08, "kd": 25.0},
  "bottomPid": {"kp": 8.0, "ki": 0.04, "kd": 18.0},
  "logs": ["I (14200) FSM: Entering REFLOW phase..."]
}
```

### Delta Log Streaming
To minimize network bandwidth over Wi-Fi, the system uses an atomic monotonic log sequence counter (`s_logSequence`). Logs are only transmitted when new entries have been appended, reducing idle bandwidth consumption by over 70%.

---

## 5. Zero-Copy Flash-Embedded Static Assets & Frontend Architecture

The Web UI is delivered natively from ESP32-S3 ROM flash memory without external CDN dependencies. Source assets located in `assets/` are organized into a decoupled, modular architecture and bundled into production payloads via `tools/pack_web.py` using **100% Atomic File Replacement** (`.tmp` -> OS rename):

### Modular Frontend Source Structure (`assets/`)

```
assets/
├── index.html                  # Semantic HTML5 shell, ARIA progress bars, zero inline styles
├── style.css                   # Bundled production stylesheet (compiled from assets/css/)
├── app.js                      # Bundled production application engine (compiled from assets/js/)
├── chart.min.js                # Local standalone Chart.js v4.4.1 (100% offline, zero-CDN)
├── lang/
│   └── de.js                   # German localization dictionary (100% key coverage)
├── css/                        # 7 Decoupled CSS Domain Modules
│   ├── 01_variables.css        # Design tokens, CSS custom properties, Dark/Light palettes
│   ├── 02_base.css             # Base reset, box-sizing, touch-action, scrollbars, utilities
│   ├── 03_header.css           # 3-column navbar, brand, nav pills, status badge, CLI log dropdown
│   ├── 04_chart.css            # Live chart container, HUD badges, control strip, actor glow/spin
│   ├── 05_tabs.css             # Tab panels, accordions, form controls, profile & PID tables
│   ├── 06_backup.css           # Backup portal, orb hero stage, pulse rings, OTA upload card
│   └── 07_modal.css            # Glassmorphism modal dialogs (alert/confirm/prompt), system info card
└── js/                         # 14 Layered JavaScript Modules (5-Layer Hierarchy)
    ├── 01_store.js             # Layer 1: Central reactive state store, validation limits, DOM helpers
    ├── 02_i18n.js              # Layer 1: Translation engine, language bundle loader, fallback strings
    ├── 03_utils.js             # Layer 1: DIN 1333 mathematical formatting, time formatting, DOM setters
    ├── 04_api.js               # Layer 2: Fault-tolerant REST client, AbortController timeout (5s), error translation
    ├── 05_modal.js             # Layer 2: Asynchronous modal system (alert, confirm, prompt, custom)
    ├── 06_chart.js             # Layer 3: Chart.js instance, datasets, ring buffer (14,400 pts), CSV/PNG export
    ├── 07_fsm.js               # Layer 3: FSM state badge, system logs, actor overrides, interlocks, Screen Wake Lock
    ├── 08_hud.js               # Layer 3: Real-time Cockpit HUD updater, TAL > 217°C, Delta T, settle/hold
    ├── 09_ws.js                # Layer 3: Resilient WebSocket client, 3.5s watchdog, visibility wake lifecycle
    ├── 10_tab_profiles.js      # Layer 4: Profile CRUD, dual-heater table editor, profile payload validation, debouncing
    ├── 11_tab_settings.js      # Layer 4: Machine settings form binding, Wi-Fi security, settings validation, debouncing
    ├── 12_tab_tunepid.js       # Layer 4: PID library management, autotune strip, gain interpolation preview, debouncing
    ├── 13_tab_backup.js        # Layer 4: Backup portal, orb hero stage, in-browser CRC32 ZIP generator, 60s OTA timeout
    └── 14_app.js               # Layer 5: Tab switching, theme preferences, beforeunload tab protection, bootstrap

```

### Build-Time Packaging & ROM Embedding

Pre-compressed with GZIP via `tools/pack_web.py` and embedded into the application ROM flash using `EMBED_FILES` in `CMakeLists.txt`:

* `index.html.gz` $\rightarrow$ `_binary_index_html_gz_start` (`text/html; charset=utf-8`)
* `style.css.gz` $\rightarrow$ `_binary_style_css_gz_start` (`text/css`)
* `app.js.gz` $\rightarrow$ `_binary_app_js_gz_start` (`application/javascript`)
* `chart.min.js.gz` $\rightarrow$ `_binary_chart_min_js_gz_start` (`application/javascript`)
* `de.js.gz` $\rightarrow$ `_binary_de_js_gz_start` (`application/javascript`)

* **Flash ROM Budget:** Total compressed payload is strictly guarded under **160 KB** (currently **149.69 KB**, saving 75.3% flash space).
* **Automated CI Quality Guard:** `tools/check_frontend.py` enforces zero `var`, strict equality (`===`/`!==`), offline/zero-CDN compliance, touch-action standards, zero inline styles, and 100% I18n coverage.
* **Atomic Replacement:** `tools/pack_web.py` writes `.tmp` files and swaps them atomically into `components/web/build_assets/` via OS rename, eliminating race conditions with parallel Ninja/CMake builds.

Assets are served with `Content-Encoding: gzip` directly from flash without allocating RAM buffers. Path traversal attempts (`..`) are blocked with HTTP 403 Forbidden.

### Caching Policy & Cache-Busting
* **Static Assets (CSS, JS, Language Bundles):** Served with long-term cache header `Cache-Control: public, max-age=604800, immutable` (7 days). Automated cache-busting is enforced via query parameter versioning (`?v=<version>`) injected during packaging by `tools/pack_web.py`.
* **HTML Document (`index.html`):** Served with `Cache-Control: no-cache, no-store, must-revalidate` to ensure browser clients always check for new application shells on reload.

### Captive Portal Detection
The static server intercepts standard operating system captive portal probes and responds appropriately to trigger immediate browser popups:
* **Apple (iOS / macOS):** `hotspot-detect.html`, `canonical.html` $\rightarrow$ `<HTML><BODY>Success</BODY></HTML>`
* **Android / Google:** `generate_204`, `gen_204` $\rightarrow$ HTTP 204 No Content
* **Microsoft Windows:** `ncsi.txt`, `connecttest.txt` $\rightarrow$ `Microsoft NCSI` / `Microsoft Connect Test`

---

## 6. Wi-Fi SoftAP, mDNS & Captive DNS

The `WifiManager` coordinates network connectivity:
* **Access Point:** Default SSID `BGA Reflow Controller`, default WPA passphrase `reflow123` (persisted in NVS namespace `wifi_sec`).
* **mDNS Responder:** Registers hostname `reflow.local` (`http://reflow.local`), enabling instant zero-configuration access from macOS, iOS, Windows, and Linux.
* **Captive DNS Server:** Asynchronous FreeRTOS task listening on UDP Port 53, redirecting all incoming DNS queries to the default gateway IP (`192.168.4.1`).

---

## 7. Unit Testing & Verification (`test_web.cpp`)

The component includes 21 Unity unit tests validating serialization precision, MIME mappings, payload security, REST routing, WebSocket management, and server lifecycle:

| # | Test Function | Verified Behavior |
|---|:---|:---|
| 1 | `test_web_telemetry_defaults` | Asserts default state, temperature zeroes, and empty marker vectors on initialization. |
| 2 | `test_web_mime_types` | Validates MIME Content-Type mapping for HTML, GZ, CSS, JS, JSON, and SVG files. |
| 3 | `test_web_json_serialization_deep` | Verifies end-to-end JSON construction and parsing round-trips for telemetry snapshots. |
| 4 | `test_web_telemetry_precision_serialization` | Asserts decimal precision formatting rules (1 decimal for temperature, 2 for PID). |
| 5 | `test_web_profile_path_param_extraction` | Validates URI path extraction and query string parameter fallback logic. |
| 6 | `test_web_mismatched_payload_detection` | Asserts payload content detection rejecting machine settings uploaded to profile endpoints. |
| 7 | `test_web_domain_model_range_validations` | Verifies strict schema boundary enforcement on incoming JSON payloads. |
| 8 | `test_web_wifi_constants` | Asserts default SSID, password lengths, and NVS namespace constants. |
| 9 | `test_web_log_buffer_capture` | Verifies circular ring buffer retention, FIFO popping at capacity, and sequence monotonicity. |
| 10 | `test_web_post_report_registration` | Validates Power-On Self-Test (POST) diagnostic structure registration and formatting. |
| 11 | `test_web_telemetry_nan_inf_serialization` | Verifies RFC 8259 compliance ensuring NaN and Inf fields serialize as 'null' / 0 and parse cleanly. |
| 12 | `test_web_wifi_password_validation` | Validates minimum length and sanitization rules for Wi-Fi passwords. |
| 13 | `test_web_static_file_handler` | Verifies static file dispatch, gzip delivery headers, and 404 response handling. |
| 14 | `test_web_ws_client_management` | Validates WebSocket client tracking, array bounds safety, and disconnection handling. |
| 15 | `test_web_routes_status_and_theme` | Tests `/api/status` and `/api/theme` GET/POST endpoints and JSON schema integrity. |
| 16 | `test_web_routes_settings` | Tests `/api/settings` retrieval, parsing, and atomic update dispatch to Core 1. |
| 17 | `test_web_routes_control_and_overrides` | Tests `/api/control` and `/api/overrides` command queue bridging. |
| 18 | `test_web_routes_pid_and_profiles` | Tests PID library endpoints and profile listing retrieval. |
| 19 | `test_web_routes_profiles_crud` | Tests full CRUD lifecycle of profile upload, modification, and deletion. |
| 20 | `test_web_routes_ota` | Tests firmware OTA upload streaming, header parsing, and validation. |
| 21 | `test_web_server_lifecycle` | Validates complete WebServer start, route binding, and graceful stop lifecycle. |

---

## 8. CMake Integration

Defined in [`CMakeLists.txt`](CMakeLists.txt):

```cmake
idf_component_register(
    SRCS 
        "src/api_context.cpp"
        "src/control_routes.cpp"
        "src/profile_routes.cpp"
        "src/ota_routes.cpp"
        "src/system_routes.cpp"
        "src/wifi_manager.cpp"
        "src/web_server.cpp"
        "src/rest_api.cpp"
        "src/static_file_server.cpp"
        "src/ws_handler.cpp"
    INCLUDE_DIRS
        "include"
        "../../main/include"
    REQUIRES 
        config
        storage
        fsm
        esp_wifi
        esp_netif
        esp_http_server
        mdns
        nvs_flash
        freertos
        log
        cjson
        lwip
        esp_app_format
        app_update
        bootloader_support
    EMBED_FILES
        "build_assets/index.html.gz"
        "build_assets/style.css.gz"
        "build_assets/app.js.gz"
        "build_assets/chart.min.js.gz"
        "build_assets/de.js.gz"
)
```

Managed dependencies via `components/web/idf_component.yml`:
* `espressif/mdns`: mDNS responder service for ESP-IDF

---

## 9. 📜 License

Copyright (C) 2026 EM-OpenTech

This component is part of the BGA Reflow Controller project and is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)**.

* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `AGPL-3.0-or-later`

See the project root [`LICENSE`](../../LICENSE) file for the full license text.
