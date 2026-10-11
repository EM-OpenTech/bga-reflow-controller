# Web Server, REST API & Telemetry Component (C++20)

Embedded asynchronous Web communication and user interface engine for the **BGA Reflow Controller**, running natively on **ESP-IDF v6.0.2** and **FreeRTOS**.

This component manages the HTTP 1.1 web server, WebSocket live telemetry broadcast engine (5 Hz), REST API endpoints, Zero-Copy ROM-embedded static asset delivery, captive portal DNS redirection, SoftAP Wi-Fi provisioning with NVS credential persistence, and seamless 1-Click browser OTA firmware updates.

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
        - SoftAP (WPA2/WPA3 Mixed)                         - Max 10 Sockets (LRU Purge)
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
  - Zero-Copy ROM Flash             - Full JSON CRUD             - Async Broadcast (5 Hz)
  - Embedded GZIP Assets            - Atomic Cross-Core Queue    - Delta Log Streaming
  - Captive Portal Probes           - Live System POST Report    - Ring Buffer (30 lines)
              |                              |                              |
              v                              v                              v
       (Browser Client)               (Core 1 Queue)                 (Browser Client)
```

### Cross-Core Concurrency & Isolation
* **Asynchronous FSM Command Queue (`fsmCmdPost`):** The REST API running on Core 0 never mutates FSM state directly. All commands (`PREHEAT`, `REFLOW`, `STOP`, `SKIP_STEP`, `RESET_FAULT`, `AUTOTUNE_START`, `RELOAD_SETTINGS`) are posted into a lock-free FreeRTOS queue (`g_fsmCmdQueue`) and safely drained by Core 1.
* **Double-Buffered Telemetry Consumption:** Telemetry frames broadcasted over WebSocket are read from a lock-free atomic snapshot mailbox (`AppController::getSensorSnapshot()`), eliminating mutex contention between Core 0 and Core 1.
* **Socket Health & LRU Purging:** The HTTP daemon is configured with LRU socket purging (`lru_purge_enable = true`) and socket timeouts of 3 seconds (`recv_wait_timeout = 3`, `send_wait_timeout = 3`, `so_linger = 0`) to prevent socket starvation from flaky Wi-Fi clients.

---

## 2. Component Modules

| Module | Source Files | Description |
| :--- | :--- | :--- |
| **`WebServer`** | `web_server.hpp`<br>`web_server.cpp` | Central coordinator for HTTP daemon lifecycle, route dispatching, and UART log hooking. |
| **`RestApi`** | `rest_api.hpp`<br>`rest_api.cpp` | Comprehensive REST API dispatcher (GET/POST/DELETE) with JSON schema validation and limits enforcement. |
| **`WebSocketHandler`** | `ws_handler.hpp`<br>`ws_handler.cpp` | Real-time 5 Hz telemetry distributor with dead socket reclamation and delta log synchronization. |
| **`StaticFileServer`** | `static_file_server.hpp`<br>`static_file_server.cpp` | Zero-copy GZIP static file server serving ROM-embedded Web UI assets with OS captive portal probes. |
| **`WifiManager`** | `wifi_manager.hpp`<br>`wifi_manager.cpp` | Wi-Fi SoftAP manager, NVS security storage, mDNS responder (`reflow.local`), and captive DNS server. |
| **`FsmCommandQueue`**| `fsm_command_queue.hpp` | FreeRTOS command queue bridge guaranteeing race-free communication between Core 0 and Core 1. |

---

## 3. REST API Specification

### System Telemetry & Configuration
* **`GET /api/status`**: Complete diagnostic snapshot including active FSM state, live temperatures, FreeRTOS stack high-watermarks (`safety`, `burstfire`, `control`, `input`, `web`), heap memory headroom, boot POST diagnostics, LittleFS storage wear-leveling metrics, and compile-time validation limits.
* **`GET /api/settings`**: Returns current machine settings (thermal limits, watchdog windows, PID gains, sensor calibration offsets).
* **`POST /api/settings`**: Validates and persists new machine settings to LittleFS, then posts `FsmCommandType::RELOAD_SETTINGS` to Core 1.
* **`GET /api/theme`** / **`POST /api/theme`**: Convenience wrappers for user UI theme preference (`dark` / `light`).

### Profile Management
* **`GET /api/profiles`**: Returns array of all stored reflow profiles and active profile selection.
* **`GET /api/profiles/<filename>`** or `GET /api/profile?name=<filename>`: Fetches specific profile JSON definition.
* **`POST /api/profiles`**: Validates and saves profile JSON. Protected against overwriting the factory profile (`factory-profile.json`).
* **`DELETE /api/profiles/<filename>`**: Removes profile from LittleFS. Rejects deletion of factory profile.

### Process Control & Overrides
* **`POST /api/control`**: Dispatches process commands to Core 1 queue:
  * `{"command":"start","profile":"..."}` $\rightarrow$ `PREHEAT`
  * `{"command":"reflow"}` $\rightarrow$ `REFLOW`
  * `{"command":"stop"}` $\rightarrow$ `STOP`
  * `{"command":"skip"}` $\rightarrow$ `SKIP_STEP`
  * `{"command":"resetFault"}` $\rightarrow$ `RESET_FAULT`
  * `{"command":"autotune","channel":"top","target":150}` $\rightarrow$ `AUTOTUNE_START`
* **`POST /api/overrides`**: Toggles manual cooling fan or work lamp overrides via `AppController`.

### Tuning & Security
* **`GET /api/pidlibrary`** / **`POST /api/pidlibrary`**: CRUD access to multi-point scheduled PID tuning curves (`pid_library.json`).
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

Clients connect via WebSocket to `/ws` to receive real-time telemetry frames at 5 Hz (200 ms). Up to **4 concurrent WebSocket clients** are supported.

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

## 5. Zero-Copy Flash-Embedded Static Assets

Web assets are pre-compressed with GZIP via `tools/pack_web.py` and embedded into the application ROM flash using `EMBED_FILES` in `CMakeLists.txt`:

* `index.html.gz` $\rightarrow$ `_binary_index_html_gz_start` (`text/html; charset=utf-8`)
* `style.css.gz` $\rightarrow$ `_binary_style_css_gz_start` (`text/css`)
* `app.js.gz` $\rightarrow$ `_binary_app_js_gz_start` (`application/javascript`)
* `chart.min.js.gz` $\rightarrow$ `_binary_chart_min_js_gz_start` (`application/javascript`)
* `de.js.gz` $\rightarrow$ `_binary_de_js_gz_start` (`application/javascript`)

Assets are served with `Content-Encoding: gzip` directly from flash without allocating RAM buffers. Path traversal attempts (`..`) are blocked with HTTP 403 Forbidden.

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

The component includes 10 Unity unit tests validating serialization precision, MIME mappings, payload security, and data models:

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

---

## 8. CMake Integration

Defined in [`CMakeLists.txt`](CMakeLists.txt):

```cmake
idf_component_register(
    SRCS 
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
