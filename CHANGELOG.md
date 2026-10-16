# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added

* **UI & Hardware Parity for Fault Reset (`feat(ui,web,main)`)**:
  * Dynamically transformed the web UI Stop button into a high-visibility yellow "Reset Fault" button while the controller is in `FAULT` state.
  * Routed UI fault reset requests through `resetFault` control action, invoking `app->getSafety().reset()` and `app->getFsm().resetFault()` to release the hardware SSR inhibit synchronously.
  * Added backend fallback mapping `'stop'` commands to `resetFault` when in `FAULT` state to mirror physical STOP button semantics.
* **Fault State Settings Recovery (`components/web`, `components/safety`, `main`)**:
  * Enabled modification and saving of machine settings and PID library points during `FAULT` state in both the REST API and web UI.
  * Dispatched `RELOAD_SETTINGS` immediately to Core 1 in `FAULT` state, allowing operators to fix invalid thresholds, cold-junction offsets, or disable the safety watchdog without being locked out.
  * Enabled settings JSON file restoration while in `FAULT` state for swift configuration recovery.
* **Factory Default Profile Overwrite Protection (`components/storage`, `components/web`, `assets`)**:
  * Enforced server-side write protection blocking attempts to overwrite `factory-profile.json` with `HTTP 403 Forbidden` across LittleFS `StorageManager::saveProfile()` and REST API `POST /api/profiles`.
  * Added client-side pre-validation in `app.js` with localized dialogs guiding users to save copies under custom names.
* **Automatic PID Tuning Point Sorting (`components/fsm`)**:
  * Automatically sorts PID tuning libraries ascending by temperature (`std::sort`) upon loading or runtime updates in `ReflowFSM::setPidLibrary()`.

### Changed

* **Comprehensive IEEE 754 NaN & Infinity Immunity Across All Layers**:
  * **FSM & PID Gain Scheduling (`components/fsm`, `components/pid`)**: Added zero-division protection in `interpolatePidGains()` when $\Delta T \le 0.001^\circ\text{C}$; NaN/Inf guards on setpoints, inputs, and tunings; instant abort of autotuning with 0.0% power on sensor NaN.
  * **Actuator & Power Drivers (`components/output`)**: `BurstFire::setPower()` forces 0.0% power when encountering NaN or Inf values to prevent runaway heating.
  * **Thermal Simulator (`components/simulation`)**: Sanitized heater power inputs against NaN, Inf, and negative values with $[0.0, 100.0]$ clamping and ambient floor clamps.
  * **Web Telemetry Stream (`components/web`)**: Formats non-finite numbers (NaN/Inf) as JSON `null` to comply with RFC 8259; guarded integer rounding and telemetry charts against NaN crashes.
  * **Domain Validation Models (`components/config`)**: Added `std::isnan` guards across all configuration structures (`MachineSettings`, `ReflowProfile`, `PidLibrary`).
* **Concurrency & Multicore Thread-Safety Hardening**:
  * Converted `SafetyWatchdog::_fault` to `std::atomic<SafetyFault>` with `memory_order_relaxed` for lock-free, race-free multi-core access.
  * Converted all actuator state flags (`_ssrTopState`, `_ssrBottomState`, `_fanState`, `_lampState`, `_buzzerState`) in `OutputManager` to `std::atomic<bool>`.
  * Enforced recursive mutex locking across all public storage helpers (`readTextFile`, `writeTextFileAtomic`, `fileExists`).
* **Telemetry Capacity & Formatting Enhancements**:
  * Doubled `MAX_STEP_MARKERS` from 10 to 20 to comfortably accommodate full dual-heater profiles (10 top + 10 bottom steps).
  * Stripped ANSI terminal escape color codes from WebSocket log buffer and expanded log entry capacity to 384 bytes with UTF-8 preservation.
* **Documentation Synchronization**:
  * Synchronized architectural `README.md` files across all 11 firmware components to reflect recent audit findings, register maps, and thread-safety models.

### Fixed

* **Process Start Safety Interlocks (`components/fsm`, `components/web`, `main`)**:
  * Hardened `ReflowFSM::startPreheat()` and `startAutotune()` against starting when hardware outputs are inhibited.
  * Blocked Preheat, Reflow, and Autotune execution in `control_task` and REST API if `app->getSafety().hasFault()` is active.
  * Ensured `hasFault()` respects `enableSafetyWatchdog` bypass in `SafetyWatchdog`.
* **Sensor Fault Log Flooding (`components/sensor`)**:
  * Implemented state-change edge detection (`_faultLogged`) in `sensor::MAX31856` to prevent repetitive 5 Hz console log spam during persistent sensor detachments or hardware faults.
* **Web Frontend Response Stream Consumption Bug (`assets/app.js`)**:
  * Fixed `apiRequest()` to buffer response text before JSON parsing, resolving a `TypeError: body stream already read` crash when receiving non-JSON HTTP 4xx/5xx error payloads.
* **MAX31856 Driver Optimization & Cleanups (`components/sensor`)**:
  * Removed dead private `getLatest()` method and redundant cache assignments.
  * Added SPI write verification and register readback checks for CJTO and threshold registers.
* **Input Hardware Pin Configuration Guard (`components/input`)**:
  * Prevented contradictory GPIO pull-up and pull-down configurations in `InputManager::initChannel()`.
* **Storage Lifecycle & Boundary Guards (`components/storage`)**:
  * Enforced `!_mounted` checks across all LittleFS storage operations.
  * Rejected profile saving with empty filenames to prevent directory target corruption.
  * Added validation fallback to clean defaults when loading corrupted `MachineSettings`.

---

## [1.0.0-beta.3] - 2026-10-11

### Added

* **LittleFS Flash Wear-Leveling & Storage Quota Protection (`components/storage`)**:
  * Implemented real-time flash wear-leveling monitoring in `StorageManager`: queries partition usage before write operations (`esp_littlefs_info`).
  * Enforced dual-threshold safeguards: `STORAGE_WARN_PERCENT = 80%` (advisory warning log) and `STORAGE_MAX_PERCENT = 90%` (hard write block for new files, while preserving atomic updates of existing files).
  * Exposed live storage metrics (`totalBytes`, `usedBytes`, `freeBytes`, `usedPercent`, `wearLevelingWarning`, `storageLimitReached`) under the `storage` telemetry object in `GET /api/status`.
  * Added frontend localized error handling with `localizeApiError()` in `app.js` and German translation key `modal.storage_limit_rejected` in `assets/lang/de.js`.
  * Added unit test #12 (`test_storage_wear_leveling_limits`) in `components/storage/test/test_storage.cpp`.
* **Component Architecture Documentation & Audit Standardization**:
  * Added dedicated architectural `README.md` documents across all firmware components:
    * `components/config`: Central limits, compile-time timing, pin mappings, and build-system configuration.
    * `components/fsm`: Dual-channel hierarchical state machine, emergency latching, profile execution, and state transitions.
    * `components/input`: 50 Hz hardware switch debouncing, short/long-press detection, and master overrides.
    * `components/output`: Zero-crossing burst-fire PWM, hardware polarity inversion, and dual safety/simulation lockouts.
    * `components/pid`: C++20 port of QuickPID (P-on-M / P-on-E, integral windup guards) and sTune autotuner.
    * `components/QuickPID-3.1.9_Library` & `components/sTune-2.4.0_Library`: Upstream library wrapper documentation.
    * `components/safety`: Real-time hazard detection matrix, stuck SSR inversion, and dynamic PCB plateau bypass.
    * `components/sensor`: MAX31856 datasheet compliance, register math, SPI bus timings, and brownout auto-recovery.
    * `components/simulation`: Dual-channel lumped-parameter thermal simulator with ambient coupling and noise modeling.
    * `components/storage`: LittleFS wear-leveling quotas, atomic staging, schema migration, and factory profile protection.
    * `components/web`: Asynchronous HTTP 1.1 daemon, 5 Hz WebSocket telemetry, zero-copy ROM flash assets, captive portal DNS, and browser OTA.
    * `main`: Master application entry point, 8-phase boot pipeline, POST diagnostics, and dual-core FreeRTOS task coordinator.
* **Simulation Mode Hardware Safety Lockout (`OutputManager::setSimulationMode`)**:
  * Enforced driver-level hardware lockout in `OutputManager`: when simulation mode is active (`_simulationMode`), physical SSR control outputs (Top and Bottom) are strictly forced OFF and all activation requests are ignored.
  * Added lock-free `std::atomic<bool> _simulationMode` with unit test `test_output_simulation_mode()` in `components/output/test/test_output.cpp`.
  * Synchronized simulation state across `AppController::begin()`, `reloadSettingsAndPidLibrary()`, and `burstfire_task.cpp` to eliminate bench hazards during dry runs.
* **MAX31856 In-Flight Power-On Reset (POR) & Brownout Auto-Recovery**:
  * Expanded cyclic sensor read to a 16-byte atomic burst (`0x00` CR0 through `0x0F` SR).
  * Implemented real-time detection of unexpected hardware resets (CR0 CMODE=0 / MASK=0xFF) with automatic in-flight re-initialization of configuration and threshold registers without requiring an MCU restart.
  * Added unit test #20 for POR detection in `components/sensor/test/test_sensor.cpp`.
* **Lock-Free Single-Writer Double-Buffered Sensor & Telemetry Mailbox**:
  * Implemented `AppController::SensorSnapshot` double-buffered mailbox using `std::atomic<uint8_t>` with acquire/release memory semantics.
  * Established `control_task` on Core 1 as the exclusive SPI bus master (Single-Writer), completely eliminating cross-core bus contention.
  * `safety_task` now consumes consistent snapshots without unsynchronized cross-task reads or mutex locking.
  * Propagated sensor health flags (`topSensorOk`, `bottomSensorOk`) through `SystemContextData` to WebSocket telemetry.
* **Release Binary Build Automation (`tools/build_factory_bin.py` & `CMakeLists.txt`)**:
  * Integrated automated generator script for monolithic 16 MB factory images (`firmware-v<version>.bin`, flash at offset `0x0`) and compact OTA application binaries (`ota-update-v<version>.bin`).
  * Bound to CMake via custom target `factory_bin ALL`, automatically producing production-ready release binaries on every build.
  * Added automatic CMake reconfiguration dependencies (`CMAKE_CONFIGURE_DEPENDS`) for web assets and `version.txt`.
* **Standalone ESP32-S3 Core Dump Inspector (`tools/coredump_reader.py`)**:
  * Created Python utility to decode and inspect ESP32-S3 ELF core dumps.
  * Parses FreeRTOS task lists, stack frames, CPU registers (PC, PS, A0-A15), and crash causes.
* **Diagnostics & Stack Hardening in `sdkconfig.defaults`**:
  * Enabled FreeRTOS Canary Stack Overflow Detection (`CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y`).
  * Increased main task boot stack to 8192 bytes (`CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`) to accommodate dual-sensor setup and storage mounting.
  * Enabled serial terminal ANSI colors (`CONFIG_LOG_COLORS=y`).

### Changed

* **Control Loop & Thermal Simulation Unified to 5 Hz (200 ms)**:
  * Unified central `config::Timing::CONTROL_LOOP_PERIOD_MS = 200` (5 Hz) across `control_task`, PID controllers, FSM runner, and `ThermalSimulator`.
  * Matches the MAX31856 4-sample continuous conversion cycle (160 ms), eliminating derivative sampling jitter and artificial zero-delta spikes.
* **MAX31856 Timing & Hardware Sequencing Hardening**:
  * Set SPI clock speed to 200 kHz with `cs_ena_posttrans=1` (5 µs) and `input_delay_ns=80` (tCDD).
  * Robust register sequencing (modeled after the official MAX31856 reference driver): force CMODE=0 before modifying CR1 or filter notch registers to prevent ADC state machine glitches.
  * Added dynamic `conversionTimeMs()` calculation following datasheet Table 2.
  * Optimized startup priming with parallel dual-sensor initialization, saving ~340 ms boot time.
  * Added post-initialization `verifyConfig()` readback (`0x00`..`0x09`) with verification logging.
* **ESP-IDF v6.0.2 & ESP32-S3 N16R8 Best-Practice Optimization**:
  * Set CPU clock to 240 MHz (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y`).
  * Flash mode configured to DIO @ 80 MHz (`CONFIG_ESPTOOLPY_FLASHMODE_DIO=y`).
  * Octal PSRAM configured to OPI DDR @ 80 MHz with 64-byte cache alignment and `CONFIG_SPI_MASTER_IN_IRAM=y`.
  * Increased HTTP server buffer limits (`CONFIG_HTTPD_MAX_REQ_HDR_LEN=2048`, `CONFIG_HTTPD_MAX_URI_LEN=1024`, `CONFIG_HTTPD_ERR_RESP_NO_DELAY=y`).
* **Web Telemetry & REST API Refactoring**:
  * Centralized hardware diagnostic fields and reset reasons exclusively under the nested `post` object in `GET /api/status`.
  * Implemented delta log streaming with atomic sequence tracking (`s_logSequence`) in WebSocketHandler, reducing idle bandwidth by ~70%.
  * Dispatched runtime settings updates via `FsmCommandType::RELOAD_SETTINGS` on Core 1 in IDLE, preventing race conditions.
* **Frontend UI Fault Tolerant Rendering**:
  * Updated `app.js` to render `--` on temperature badges, ramp rate, and Delta T during sensor faults.
  * Prevented plotting corrupted 0.0°C readings on the live chart during sensor disconnects.
  * Implemented O(1) DOM element cache (`_domCache`) eliminating DOM queries during 2 Hz rendering.
  * Removed deprecated button debounce configurations across config models, REST payloads, storage serializers, and UI.
* **Web Asset Packaging (`tools/pack_web.py`)**:
  * Added target directory cleanup prior to asset compression, preventing orphaned files and duplicates.

### Fixed

* **MAX31856 Spinlock Removal & Register Hardening**:
  * Removed all `portENTER_CRITICAL` spinlocks from MAX31856 driver; concurrency is handled via Single-Writer architecture in `control_task`.
  * Protected `_faultStreak` counter against `uint8_t` overflow wrapping past 255 (unit test #19).
  * Fixed undefined behavior in `writeThresholds()` with `std::clamp` and `std::round`.
  * Masked reserved LTCBL bits [4:0] with `0xE0` and added register map boundary checks.
  * Removed `clearFaultRegister()` from cyclic read to prevent register race conditions.
  * Gated EMA filter seeding on `rawFault == 0` to prevent corrupt initial baseline.
* **Web Server Logger Hook Recursion & Stack Corruption**:
  * Fixed Xtensa `va_list` reuse corruption by adding `va_copy` and `va_end` in `customLogHook`.
  * Added `xPortInIsrContext()` bypass and `std::atomic<bool>` CAS re-entrancy lock to prevent logging deadlocks.
  * Hardened WebSocket log buffer with `std::try_to_lock` to avoid blocking network tasks.
  * Removed custom `close_fn` in `web_server.cpp` to prevent double-free crashes during HTTP/WebSocket connection teardown.
* **HTTP 431 / 400 Bad Request Errors**:
  * Increased HTTP header buffer to 2048 bytes, resolving browser header truncation and connection drops.

### Security

* **Storage Flash Quota Denial-of-Service Protection**:
  * LittleFS write operations block creation of new files once partition utilization reaches 90% (`STORAGE_MAX_PERCENT`), preventing complete flash exhaustion, wear-leveling degradation, or file system corruption from malicious or runaway profile uploads.
* **Actuator Hardware Simulation Lockout**:
  * Actuator signals for Top and Bottom SSRs are strictly intercepted and suppressed at the hardware abstraction layer (`OutputManager`), eliminating physical heater activation risk during dry runs.
* **Atomic Safety & Lockout Flags**:
  * Converted `OutputManager::_inhibit` and `_simulationMode` to `std::atomic<bool>` for instant lock-free cross-core safety lockout.


---

## [1.0.0-beta.2] - 2026-10-04

### Added

* **Comprehensive Dual-Core Task Watchdog Timer (TWDT)**:
  * Integrated explicit task watchdog registration (`esp_task_wdt_add`) and cyclic feeding (`esp_task_wdt_reset`) across all 5 FreeRTOS application tasks on Core 0 (`input_task`, `web_task`) and Core 1 (`safety_task`, `burstfire_task`, `control_task`).
  * Explicitly persisted TWDT configuration in `sdkconfig.defaults` (5-second timeout, Core 0 & Core 1 monitoring).
* **Hardware Cold-Junction Temperature Offset (`CJTO`)**:
  * Implemented MAX31856 register `0x09` (`CJTO`) writing in `sensor::MAX31856::writeThresholds()`.
  * Dynamically bound `topCjOffset` and `bottomCjOffset` from `MachineSettings` to both thermocouple ICs at boot and runtime reload.
* **Safety Watchdog Bypass Toggle (Testbench/Tuning Mode)**:
  * Added `enableSafetyWatchdog` boolean field to `MachineSettings`, persisted in `settings.json`.
  * `safety_task.cpp` completely skips all watchdog checks when the field is `false`, allowing safe PID autotuning on DC testbench setups.
  * UI toggle exposed in the Settings tab; active bypass indicated by a pulsing `⚠️ SAFETY BYPASSED` badge in the navigation bar.
  * Badge state is now driven exclusively from `/api/settings` (loaded on page load and settings save) — no longer sourced from the 2 Hz WebSocket stream.

### Changed

* **MAX31856 Atomic 6-Byte Burst Read**:
  * Optimized temperature, cold-junction, and fault status acquisition into a single atomic SPI burst transfer (`0x0A`..`0x0F`), eliminating data tearing and reducing SPI bus transaction overhead to $< 50\,\mu\text{s}$.
* **Hardware Register Limit Synchronization**:
  * Aligned cold-junction calibration offset boundaries in `machine_config.hpp`, `index.html`, and `app.js` to the physical 8-bit signed register limits of the MAX31856 ($-8.0^\circ\text{C} \dots +7.9^\circ\text{C}$).
* **Dynamic Constructor PID Initialization**:
  * Replaced hardcoded default PID parameters in `AppController` with central values from `config::MachineSettings`.
* **Frontend UI Palette Harmonization**:
  * Unified all green status elements (preheat-done blinking badge, status-log items, connection indicators) to the system Emerald palette (`#10b981` / `#34d399` / `rgba(16, 185, 129)`).
* **BurstFire Window Timer Reset Prevention (`burst_fire.cpp`)**:
  * `setWindowMs()` now only resets `_windowStartUs` when the window duration **actually changes**. Previously, any call to `reloadSettingsAndPidLibrary()` — even with unchanged settings — reset the burst-fire timestamp, causing a brief spurious relay pulse (unintended heater activation mid-cycle).
* **REST API State Guards for Preheat and Reflow (`rest_api.cpp`)**:
  * `POST /api/control/preheat` now returns `HTTP 403 Forbidden` unless the FSM is in `IDLE`, `DONE`, or `COOLING` state.
  * `POST /api/control/reflow` now returns `HTTP 403 Forbidden` unless the FSM is in `PREHEAT` state.
  * Prevents unintended mid-process preheat restarts and out-of-sequence reflow starts originating from UI double-clicks or browser replays.
* **Control Task FSM State Guard (`control_task.cpp`)**:
  * Added FSM state check on Core 1 before executing the `PREHEAT` queue command: only proceeds when the current state is `IDLE`, `DONE`, or `COOLING`. The heap-allocated `ReflowProfile*` is always freed regardless.
  * Provides a second layer of protection independent of the REST API guard.
* **Safety Telemetry Moved from WebSocket to REST API**:
  * Removed `safetyEnabled` field from `TelemetryData` struct (`ws_handler.hpp`) and its JSON serialization (`ws_handler.cpp`).
  * Removed corresponding assignment in `web_task.cpp`. Safety bypass state is now read once via `/api/settings`, reducing WebSocket payload and eliminating a 2 Hz polling overhead.

### Fixed

* **Transient EMI Spike & Sensor Fault Suppression**:
  * Added configurable `faultStreakLimit = 3` debouncing in `sensor::MAX31856` to prevent false-positive safety trips from single-cycle EMI noise.
* **Boot-Loop Safety on Test Failure**:
  * Added configurable `CONFIG_REFLOW_HALT_ON_TEST_FAILURE` guard preventing infinite reboot loops during development testing.
* **WebSocket Async Frame Handling on Disconnect**:
  * Hardened non-blocking client eviction and immediate socket session close upon client disconnect (`EAGAIN` / error 11).
* **Documentation & Task Count Alignment**:
  * Corrected outdated 4-task references to the actual 5-task dual-core architecture in header docstrings and comments.
* **Control Button State Machine (`app.js`)**:
  * Completely rewrote `updateControlButtonsState()` to handle all 9 FSM states with correct `disabled` attribute management.
  * **STOP button is now never disabled** in any state — it remains clickable for emergency stop even during IDLE, COOLING, FAULT, and disconnected states.
  * PREHEAT button is disabled during active PREHEAT state (prevents double-triggering). START remains **enabled during PREHEAT** so the user can proceed to Reflow after preheat-done.
  * Autotune button is disabled in all active process states.
* **`startPreheat()` Frontend Guard (`app.js`)**:
  * Added a JavaScript-side early-return guard preventing `startPreheat()` API calls when the FSM is already in `PREHEAT`, `SOAK`, `REFLOW`, `AUTOTUNE`, or `BACKUP` state.

### Documentation

* **Full Codebase Audit & SPDX Standardization**:
  * Standardized all 81 C++ source, header, and test files with official `AGPL-3.0-or-later` SPDX license headers.
  * Formatted 100% of files with uniform 80-column ASCII section banners (`// ============================================================================`).
  * Added complete Doxygen annotations (`@file`, `@brief`, `@copyright`, `@see`, `@param`, `@return`) across all modules.
* **Third-Party Notices**:
  * Corrected markdown table formatting and license texts in `THIRD-PARTY-NOTICES.md`.
* **README.md — Project Status Updated**:
  * Replaced `@todo` placeholder with current `1.0.0-beta.2` development stage description.
  * Updated "Pending Hardware Tests" note to reflect that real-world testbench validation has started.


---

## [1.0.0-beta.1] - 2026-09-23

### Added

* **Native ESP-IDF v6.0.2 C++ Architecture**: The firmware was completely redeveloped from the original prototype based on the Arduino framework to a native implementation on ESP-IDF v6.0.2 and C++20.
* **Dual-Core FreeRTOS Task Orchestration**:
  * **Core 1 (Safety-Critical & Control)**:
    * `safety_task` (20 Hz / 50 ms cycle, Priority 10) – Dedicated safety watchdog monitoring temperatures, thermal runaway, and actuator health.
    * `burstfire_task` (100 Hz / 10 ms cycle, Priority 8) – Zero-Cross SSR burst firing with 1% duty-cycle resolution.
    * `control_task` (10 Hz / 100 ms cycle, Priority 7) – Deterministic Reflow Finite State Machine (FSM) and dual-channel PID control.
  * **Core 0 (Networking & UI)**:
    * `input_task` (50 Hz / 20 ms cycle, Priority 6) – Non-blocking hardware button debounce with short/long-press detection and switch polling.
    * `web_task` (2 Hz / 500 ms cycle, Priority 5) – HTTP server, REST API, and WebSocket JSON broadcast.
* **5-Fold Safety Watchdog Engine (`safety_watchdog.hpp`)**:
  * Over-temperature limit protection (Top & Bottom heaters).
  * Under-temperature & open-circuit thermocouple detection.
  * Thermal runaway & No-Rise heating rate detection.
  * Stuck SSR hardware failure detection.
  * MAX31856 hardware fault flag monitoring (Cold Junction, Open Circuit, Over/Undervoltage).
  * Hardware Safety Inhibit lockout forcing all SSR outputs OFF upon safety trip.
* **Dual-Channel Inflection Point PID Autotuner (`sTune` & `sTan`)**:
  * First-Order Plus Dead Time (FOPDT) system identification in ~½Tau.
  * Multi-Instance isolation allowing simultaneous independent autotuning of Top and Bottom heating zones.
  * 5 tuning rules supported: Cohen-Coon, No-Overshoot (CHR), Ziegler-Nichols, Damped Oscillation, Mixed.
* **Embedded Responsive Web Interface**:
  * Real-time WebSocket telemetry streaming (500 ms interval).
  * Interactive Chart.js multi-channel temperature, setpoint, and power visualization.
  * Reflow profile designer with graphical ramp rate and step duration validation.
  * PID gain scheduling and manual heater override controls.
  * Firmware ROM asset packaging via `tools/pack_web.py` (Level 9 Gzip, 141 KB Flash footprint).
* **Robust LittleFS Storage Layer (`storage_manager.hpp`)**:
  * Atomic file writes using temporary swap files (`.tmp` $\rightarrow$ rename) to protect against power loss corruption.
  * Profile validation and management (`/littlefs/profiles/*.json`).
  * System settings persistence (`/littlefs/settings.json`) and PID gain library (`/littlefs/pid_library.json`).
  * Full backup and restore via ZIP archive download/upload.
* **12 Comprehensive Unity Unit Test Suites**:
  * 100% test coverage across Configuration, Sensors, Outputs, Safety, PID, Autotuner, FSM, Storage, Web Backend, and Integration.
  * Configurable build switch via `CONFIG_REFLOW_ENABLE_UNIT_TESTS` in Kconfig / `sdkconfig.defaults`.
* **Hardware Simulation Mode (`thermal_simulator.hpp`)**:
  * Thermal mass physics simulation enabling full web interface, profile execution, and FSM testing without connected 230V hardware.

### Changed

* **Zero Profile Auto-Fallback Policy**: START button press and REST preheat commands strictly require an active user-selected profile; execution is rejected with a warning modal if no profile is loaded.
* **64-bit Hardware Timer Integration**: Replaced Arduino 32-bit `micros()`/`millis()` with ESP-IDF `esp_timer_get_time()`, eliminating 71-minute rollover bugs.
* **Modern C++20 Standardization**: Replaced legacy macros with `std::clamp`, `std::vector`, and strongly typed `enum class`.
* **Partition Table Optimization**: Configured custom 16 MB flash layout (`default_16MB_partitions.csv`) with dual 6.4 MB OTA application slots, 3.4 MB LittleFS storage, and 64 KB Core Dump partition.

### Fixed

* Fixed memory leak in sliding tangent buffer (`sTan`) by eliminating raw pointer allocations.
* Fixed race conditions between network and control tasks by implementing thread-safe `SystemContext` mutexes and `FsmCommandQueue`.
* Fixed JSON serialization buffer exhaustion by adding explicit out-of-memory guards in `storage_manager.cpp`.
* Fixed buzzer pulse duration tracking for preheat and reflow stage completion notifications.

### Security

* **Path Traversal Protection**: Enforced strict alphanumeric and whitelist filename validation on all LittleFS REST endpoints.
* **Input Sanitization**: Range-checked all numerical JSON parameters for setpoints, PID gains, and machine limits.
* **Safety Inhibit Lockout**: Actuator power is physically suppressed during any triggered safety alarm.
