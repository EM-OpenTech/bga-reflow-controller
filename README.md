# ESP32-S3 N16R8 DIY BGA Reflow Controller Firmware

> [!CAUTION]
>
> ### ⚠️ EXTREME DANGER: DIY HARDWARE, MAINS VOLTAGE & FIRE HAZARD
>
> This project is designed to control high-power heating elements and operates with **dangerous AC mains voltages (110V / 230V)**.
>
> * **USE AT YOUR OWN RISK:** Using mains electricity and high-temperature heaters carries a **severe risk of fire, severe burns, personal injury, and death**.
> * **DO NOT RELY ON SOFTWARE SAFETY MECHANISMS:** Although comprehensive software safety watchdogs (thermal runaway detection, stuck SSR detection, sensor fault monitoring) are implemented, **you must NEVER rely solely on software for protection**. Software can crash, hang, or contain bugs.
> * **MANDATORY HARDWARE SAFEGUARDS:** You **must** install independent, physical, non-resettable hardware protection mechanisms (e.g., thermal cutoffs / thermal fuses in series with heaters, properly rated mains fuses, emergency cut-off switches, and proper protective earth grounding).
> * **NO LIABILITY:** The author assumes absolutely no liability for property damage, fire, injury, or loss of life resulting from building, modifying, testing, or operating this system. The software and designs are provided strictly **"AS IS"** without warranties of any kind.
> * **Please conduct the first tests using a test setup, such as a 24V 3D printing hotend or a ceramic power resistor (driven via a DC-DC SSR or MOSFET module). If you don't know how to set this up, please do not conduct any tests at this stage of development**
>
---

## ℹ️ About This Project & Public Preview Status

* **Current Development Stage — `1.0.0-beta.5`:** Active real-world testing is ongoing. While further empirical test runs are being conducted, the architectural foundation and core feature set are now complete — this codebase may represent the first stable beta baseline. Only minor fine-tuning and polishing adjustments remain.
* **Bench Testing & Safety Watchdog Calibration Notice:**  
  * Initial hardware bench tests were conducted using a **24V 3D-printer hotend** with the Safety Watchdog intentionally bypassed/disabled (`enableSafetyWatchdog = false`) to isolate PID and FSM mechanics.
  * **Calibration Caveat:** The current factory default thresholds in the Safety Watchdog (stuck SSR window, no-rise timeout) are not yet empirically calibrated for an open, uninsulated station with high-mass ceramic infrared radiators.
  * In fast-ramping test setups, the watchdog may trigger prematurely if temperature rise rates exceed default assumptions.
  * For sluggish, high-inertia systems (e.g. large ceramic preheaters), the current "baseline default settings" should work without issues, but dedicated empirical calibration tests will follow.
  * **Safety Watchdog Bypass in Settings:** The Safety Manager can be disabled via the Web UI Settings tab (`enableSafetyWatchdog = false`). **WARNING:** Use this toggle strictly for diagnostic bench testing, with extreme caution, and entirely at your own risk — solely if false alarms occur during initial station tuning.
* **CRITICAL HARDWARE NOTICE: Mechanical Relays vs. Solid-State Relays (EMC / EMI):**  
  > [!WARNING]
  > **Do NOT drive heating elements with bare mechanical relay modules!**  
  > Mechanical contact arcing generates severe Electromagnetic Interference (EMI / inductive kickback) that couples into the power and ground planes, causing sporadic, unpredictable ESP32 resets under varying panic causes (Interrupt WDT timeouts, illegal instructions, or brownout resets). Real-world testing with additional shielding verified and confirmed this exact failure mode.  
  > **Always operate heating channels strictly with proper Solid-State Relays (SSRs)** (zero-crossing AC SSRs or optocoupled DC MOSFET stages with appropriate RC snubber / flyback protection).
* **Modular Architecture & Component Documentation:** Every submodule across `components/*` and `main/` now includes dedicated technical architectural `README.md` documentation explaining internal register mappings, mathematical formulas, and concurrency models. This modular design makes it easy for developers to fork, customize, or extend individual subsystems.
* **Community Contributions & Code Review Welcome:** Community peer reviews, optimizations, feedback, and pull requests are very welcome! If you spot potential improvements, feel free to contribute.
* **Current Focus & Future Roadmap:** The firmware currently implements a dedicated dual-channel control process for Top (dark infrared) and Bottom (preheater) zones. Future roadmap expansions envision multi-zone architectures (e.g., 3+ heating zones), alternative temperature sensor frontends (PT100/PT1000 RTDs), and extended telemetry protocols.
* **Functional Baseline & AI Multi-Model Code Reviews:** The core control engine, safety architecture, and primary state machines are thoroughly tested and verified. Comprehensive multi-pass code audits utilizing different state-of-the-art AI reasoning models have been conducted across all components. All critical, non-cosmetic vulnerabilities, concurrency deadlocks, and severe functional bugs identified during these reviews have been resolved.
* **Secondary Features & Irregular Flow Edge-Cases:** While standard reflow, preheating, autotuning, and safety monitoring flows are robust and fully functional, certain secondary convenience functions ("add-on" features or non-standard edge-case user sequences) may still require refinement or minor adjustments. These non-critical edge cases will be systematically polished and resolved in upcoming iterations.
* **Single-Person Hobby Project:** This is an open-source hobby project created and maintained by a **single private individual** (not a company, organization, or team).
* **AI-Assisted Development:** The architecture, code, and technical research were developed and designed to the best of my knowledge with the active assistance of AI tools.
* **Source File Headers Notice:** Please ignore any placeholder or inconsistent author mentions in individual file headers (such as references to an automated "team") for now. These will be cleaned up and unified prior to the official v1.0.0 release.
**The legal open-source baseline is fully established via [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).**

---

## 🎯 Overview & Key Features

This open-source firmware turns an **ESP32-S3 (N16R8)** into a high-precision, dual-channel PID reflow controller designed for **custom DIY BGA Rework Stations** (e.g., GPU reballing/ replacement, CPU socket swaps, complex SMD soldering, and board repairs) as well as reflow ovens and hotplates.

* **Dual-Zone Independent PID Control:** Simultaneous, independent control for Top Heater (Dark Infrared) and Bottom Preheater with zero-cross SSR burst firing (1% duty-cycle resolution).
* **High-Precision MAX31856 SPI Thermocouples:** Optimized for Type-K thermocouples with native cold-junction compensation, noise filtering, and hardware fault detection.
* **Integrated Web Interface & Real-Time Graphing:** Embedded HTTP & WebSocket server streaming live temperature curves, setpoints, and heater power visualized with **Chart.js** (works 100% offline). Features a decoupled 5-layer JavaScript architecture (14 modules), 7 CSS domain modules, semantic HTML5 with ARIA progress bars, zero inline styles, and atomic build-time flash packaging (< 160 KB total gzipped ROM payload).
* **Multi-Stage Reflow Profile Designer:** Create, test, validate, and save custom soldering and preheat temperature profiles directly in the browser.
* **Dual Inflection-Point PID Autotuner:** Integrated `sTune` & `sTan` autotuning engine to calculate optimal PID gains for both heating zones.
* **Multi-Tier Safety Watchdogs:** Over-temperature limits, thermal runaway detection, open-circuit thermocouple monitoring, and stuck-SSR alarms.
* **Dual-Core FreeRTOS Architecture:** Native ESP-IDF v6.0.2 C++20 architecture with safety and control pinned to Core 1 and Wi-Fi / Web UI handled on Core 0.
* **The Most Frequently Asked Question** Since I’ve been asked several times whether more than two heating zones can be controlled separately, the answer is yes—in principle, it would be possible, since the ESP32-S3 N16R8 has more than enough processing power and memory. However, no approach or design concept has been developed for this yet. In the future, once the fundamentals are in place, this additional feature can possibly be implemented without any problems.

---

## 🔌 Hardware Pin Configuration

> [!IMPORTANT]
> The default GPIO pin assignments below represent a baseline reference design for the **ESP32-S3 (N16R8)**.
> The pin assignments were chosen randomly at the time of the first compilation **and should be adjusted to match the actual hardware capabilities if necessary.**

### Editing Pin Definitions

To change the GPIO mapping for your needs, edit the configuration header:
📁 [`components/config/include/config/pin_config.hpp`](components/config/include/config/pin_config.hpp)

After modifying any pin definitions, recompile and flash the firmware.
I'm using the ESP IDF version 6.0.2

### Reference Pin Assignment

| Function | Pin (ESP32-S3) | Description |
| :--- | :--- | :--- |
| **SPI SCK** | `GPIO 12` | SPI Clock for MAX31856 Thermocouple ICs |
| **SPI MISO** | `GPIO 13` | SPI Master In Slave Out (Data from MAX31856) |
| **SPI MOSI** | `GPIO 11` | SPI Master Out Slave In (Data to MAX31856) |
| **CS Top Heater** | `GPIO 10` | Chip Select for Top Thermocouple MAX31856 |
| **CS Bottom Heater** | `GPIO 9` | Chip Select for Bottom Thermocouple MAX31856 |
| **SSR Top Heater** | `GPIO 4` | Zero-Cross Solid State Relay (Top Heating Element) |
| **SSR Bottom Heater** | `GPIO 5` | Zero-Cross Solid State Relay (Bottom Heating Element) |
| **Cooling Fan** | `GPIO 6` | Auxiliary Cooling Fan Output |
| **Inspection Lamp** | `GPIO 7` | Auxiliary Work Lamp Output |
| **Buzzer** | `GPIO 15` | Acoustic Signal Buzzer Output |
| **Start Button** | `GPIO 1` | Front-Panel Momentary Start Push-Button |
| **Stop Button** | `GPIO 2` | Front-Panel Momentary Stop / Abort Push-Button |
| **Fan Switch** | `GPIO 3` | Manual Fan Override Latching Toggle Switch |
| **Lamp Switch** | `GPIO 8` | Manual Lamp Override Latching Toggle Switch |

---

## 📶 First Connection & Web UI

> Note: This is a single, complete 16 MB firmware image.

1. First read the Quick Start Guide [Flash-Firmware.md](Flash-Firmware.md)
2. Power on the ESP32-S3. It will host its own Wi-Fi Access Point:
   * **SSID:** `BGA Reflow Controller`
   * **Password:** `reflow123`
3. Connect with your smartphone, tablet, or PC.
4. Open your browser and navigate to: **`http://192.168.4.1`** (or `http://reflow.local`)

---

## 🌐 Web Performance, Concurrency & Network Architecture

To ensure deterministic real-time control alongside responsive browser telemetry, the network and web subsystems are strictly budgeted:

### Client Concurrency Sizing (Firmware: 4 Slots | Recommended: Max. 2 Active Devices)
* **Socket Pool Budget (10 Sockets):** The ESP-IDF HTTP server maintains a total pool of 10 concurrent sockets.
* **Recommended Operator Setup:** Operating with at most **2 active physical devices** (e.g. workbench PC + smartphone/tablet) is recommended to prevent 2.4 GHz RF airtime congestion and ensure clean, jitter-free real-time telemetry (< 5 ms latency).
* **Firmware Capacity (`MAX_WS_CLIENTS = 4`):**
  * **2 Primary Dashboards:** Simultaneous operation of workbench PC + mobile device.
  * **Multi-Tab Workflows:** Supports opening a second browser tab on PC (e.g. live monitor + profile editor).
  * **Seamless Reconnect Buffer:** Absorbs page reloads ($F5$) without dropping active clients while stale sockets close.
  * **6 Dedicated HTTP Sockets:** Leaves 60% of the socket pool instantly available for REST commands, profile uploads, and emergency `STOP` aborts.

### Low-Level Network Buffer & Jitter Optimization
To eliminate transient socket disconnects (`error in send : 119` / `EINPROGRESS`) caused by 2.4 GHz RF interference or smartphone power-save sleep states:
* **TCP Send Buffer (`CONFIG_LWIP_TCP_SND_BUF_DEFAULT = 8640 B`):** Sized to $6 \times \text{MSS}$ (1440 B). Buffers 5–8 live frames during radio jitter without stalling the connection or queuing stale telemetry.
* **TCP Window Size (`CONFIG_LWIP_TCP_WND_DEFAULT = 8640 B`):** Matches send buffer to ensure smooth, unthrottled bidirectional REST and backup transfers.
* **Wi-Fi Dynamic TX Buffers (`CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM = 48`):** 50% increase in internal SRAM packet queue space for 802.11 Block-ACK retransmissions.
* **Socket Timeout (`SO_SNDTIMEO = 250 ms`):** Accommodates smartphone screen-wake delays without blocking the 500 ms `WebTask` loop.
* **Multi-Cycle Failure Toleration (`MAX_CONSECUTIVE_WS_FAILURES = 5`):** Transient packet drops are tolerated for 5 cycles (2.5 seconds) before disconnecting a dead socket.

### Long-Term Telemetry History & Memory Safety
* **3-Hour Run History:** Records up to 10,800 points (1 Hz) across 4 simultaneous curves (Top/Bottom Actual & Setpoint).
* **Automatic PSRAM Offloading:** With `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL = 16384` (16 KB), the ~216 KB history buffer is automatically placed into the 8 MB Octal PSRAM (< 3% usage), keeping internal SRAM (> 220 KB free) 100% available for Wi-Fi and DMA.
* **Chunked HTTP Streaming:** `GET /api/history` streams data in 256-byte chunks, requiring < 2 KB heap during transmission.

---

## 📜 License

Copyright (C) 2026  EM-OpenTech

This project is licensed under the **GNU Affero General Public License v3.0 (AGPLv3)** –
see the [LICENSE](LICENSE) file for the full license text.

> Under the AGPLv3, anyone who interacts with this software over a network
> (e.g. via the built-in web interface) is entitled to receive the
> corresponding source code.

For third-party libraries, bundled assets, and managed components, see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
