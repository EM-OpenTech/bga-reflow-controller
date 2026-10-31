# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added

* **Embedded Web 4-Phase Connection State Machine & Cache-Busting (`assets/`, `tools/pack_web.py`)**:
  * Implemented deterministic 4-phase frontend connection status model distinguishing `OFFLINE` (`.status-unknown`, client has no Wi-Fi/network or local IDE preview), `CONNECTING...` (`.status-connecting`, active network, 0–3.5s handshake window), `OPERATIONAL` (live FSM telemetry streaming), and `DISCONNECTED` (`.status-fault`, network active but controller unresponsive > 3.5s or connection lost).
  * Added event-driven network state tracking via `window.addEventListener('offline')` and `window.addEventListener('online')` for immediate status updates.
  * Added clean WebSocket disconnect handling on page reload/navigation via `window.addEventListener('beforeunload')` (closing with code 1000).
  * Upgraded `tools/pack_web.py` with automatic version injection from `version.txt` (`style.css?v=...`, `app.js?v=...`) to eliminate stale browser cache across firmware updates.
  * Synchronized status/error log dropdown rendering to instantly reflect offline, connecting, and disconnected states without delay upon page load or toggle.
* **Multi-Core Cppcheck Static Analysis Guard (`tools/run_cppcheck.py`)**:
  * Integrated dedicated, multi-threaded Cppcheck runner (`-j`) executing control-flow and data-flow analysis across all proprietary components using `build/compile_commands.json` (achieves ZERO issues).
  * Automatically filters vendor components and exports structured reports to `cppcheck_report.txt`.
* **Cross-Platform Tooling Suite (macOS, Linux & Windows)**:
  * Upgraded `format_all.py`, `run_cppcheck.py`, `run_clang_tidy.py`, `run_host_tests.py`, and `run_tsan_test.py` with automatic OS detection (`shutil.which`, `.exe` binary extension handling, standard Windows installation paths).
  * Graceful fallback in `run_tsan_test.py` guiding Windows users to run ThreadSanitizer in WSL2 or Linux/macOS.
* **Native Host Sanitizer Test Runner (`tools/run_host_tests.py`)**:
  * Compiles and executes 222 unit tests across 12 test suites natively on the host CPU with Clang AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).
* **Multi-Core ThreadSanitizer Concurrency Guard (`tools/run_tsan_test.py`)**:
  * Stress-tests Core 0 / Core 1 synchronization (5,000 control cycles, 20,000+ telemetry queries) under Clang ThreadSanitizer (TSan), verifying ZERO data races.
* **FreeRTOS Dynamic SMP Coredump Parser & Hardware Downloader (`tools/coredump_reader.py`)**:
  * Added dynamic 340-byte SMP Task Control Block (TCB) detection on ESP32-S3 to resolve task names (`main`, `IDLE0`, `IDLE1`, `ipc0`, `esp_timer`, `ipc1`) and recent Program Counters directly from binary core dumps without requiring matching ELF SHA256 hashes or active GDB sessions.
  * Added `--fetch` (`-f`) flag to automatically download the 64 KB coredump flash partition (`0xff0000`) directly from connected ESP32-S3 hardware using `esptool.py`, automatically detecting USB serial ports and sourcing the ESP-IDF toolchain (`~/.espressif/tools/activate_idf_v6.0.2.sh`).
  * Added automated blank/erased flash detection (`0xFF` / `0x00`) with clear diagnostic status reporting when no crash or panic has occurred.
  * Added support for decoding raw Base64 serial UART core dump blocks (`CORE DUMP START` / `CORE DUMP END`).
  * Integrated interactive `--help` documentation and usage examples directly into the CLI tool.

### Changed

* **Web Server & WebSocket Socket Stability Hardening (`components/web`)**:
  * Added `Connection: close` header to all static HTTP file responses (`static_file_server.cpp`) and REST API responses, ensuring client browsers immediately close transient download sockets to permanently reserve socket slots for the WebSocket telemetry stream.
  * Resized `clientFds` array in `ws_handler.cpp` from 8 to `MAX_HTTP_SOCKETS` (10), eliminating `ESP_ERR_INVALID_ARG` failures from `httpd_get_client_list` during connection handshakes.
  * Re-enabled LRU socket purging (`lru_purge_enable = true`) with 2.0-second send and receive timeouts in `web_server.cpp` to automatically reclaim dead client sockets.
  * Added disconnect check on `httpd_resp_send_chunk(...) != ESP_OK` in `system_routes.cpp` to abort telemetry history chunk streaming immediately upon client disconnection, eliminating 50+ `error in send : 128` log warnings during browser reloads.
  * Expanded `components/web/test/test_web.cpp` test suite from 11 to 21 tests (covering static file dispatch, WebSocket client tracking, REST routes, profile CRUD, OTA handling, and server lifecycle).
  * Synchronized `components/web/README.md` and `main/README.md` (updating total test count to 222 tests).
* **Thermal Physics Simulation Hardening (`components/simulation`)**:
  * Marked static input sanitizers (`sanitizePower`, `sanitizeTemperature`) with `[[nodiscard]]`.
  * Deduplicated sub-ambient clamping in `setTopTemperature()` and `setBottomTemperature()` through `sanitizeTemperature()`.
  * Expanded test suite in `test_simulation.cpp` from 9 to 10 tests (adding direct validation of sanitizer helper bounds).
  * Synchronized `components/simulation/README.md`.
* **Front-Panel Digital Input & Debounce Hardening (`components/input`)**:
  * Upgraded global debounce constant `DEFAULT_DEBOUNCE_MS` in `input_manager.hpp` to `inline constexpr` for strict C++20 ODR compliance.
  * Added `[[nodiscard]] bool InputSystemConfig::validate() const` enforcing physical pin sanity, preventing pin collisions, blocking simultaneous pull-up/pull-down conflicts, and protecting ESP32-S3 Octal PSRAM GPIOs (35-42).
  * Hardened `InputManager::begin()` with pre-flight configuration validation guard.
  * Marked consume-on-read query methods (`wasStartPressed`, `wasStopPressed`) with `[[nodiscard]]`.
  * Expanded test suite in `test_input.cpp` from 7 to 10 tests and fully integrated `run_input_tests()` into `tools/host_test/host_main.cpp`.
  * Synchronized `components/input/README.md`.
* **Unified LittleFS Storage Manager & JSON Persistence Hardening (`components/storage`)**:
  * Upgraded state query methods (`profileExists`, `fileExists`, `listProfiles`) with `[[nodiscard]]` and `const` qualifiers for strict read-only const-correctness.
  * Replaced loose boolean and null pointer checks with explicit comparisons against `nullptr` (`_mutex != nullptr`, `dir == nullptr`, `f == nullptr`, `root == nullptr`).
  * Expanded test suite in `test_storage.cpp` from 14 to 17 tests (adding atomic `.tmp` direct file I/O, empty file bounds, and runtime PID point deletion).
  * Boosted source-level line coverage in `storage_manager.cpp` to 80.4%, region coverage to 86.8%, and `storage_manager.hpp` to 100%.
  * Synchronized `components/storage/README.md`.
* **Reflow Finite State Machine Hardening & Emergency Cooling Protection (`components/fsm`)**:
  * Upgraded global timing, liquidus temperature, and step marker capacity constants in `reflow_fsm.hpp` to `inline constexpr` for C++20 ODR compliance.
  * Hardened `ReflowFSM::triggerFault()` with defensive `isHotOrUnknown` check (`std::isnan` / `std::isinf` evaluation) to guarantee automatic fan cooling activation during sensor disconnection or probe corruption faults while hot.
  * Enforced strict hardware SSR output inhibit rejection guards on `startPreheat()`, `startReflow()`, and `startAutotune()`.
  * Expanded test suite in `test_fsm.cpp` from 20 to 24 tests (covering emergency cooling on NaN/Inf, preheat inhibit rejection, reflow inhibit rejection, and autotune inhibit rejection).
  * Synchronized `components/fsm/README.md`.
* **Closed-Loop PID Regulation & Inflection Autotuner Hardening (`components/pid`)**:
  * Upgraded global timing and limit constants in `pid_controller.hpp` to `inline constexpr` for strict C++20 ODR compliance.
  * Marked diagnostic term inspection getters (`getPterm`, `getIterm`, `getDterm`, `getOutputSum`) as `[[nodiscard]]` and `const`.
  * Hardened `PIDController::setTunings()`, `setSetpoint()`, `setInput()`, and `setManualOutput()` with `std::isinf` guards and strict rejection of negative gains to eliminate positive thermal feedback loops.
  * Hardened `GainScheduler::interpolate()` and `interpolatePair()` against `NaN` and `Inf` inputs, guaranteeing deterministic fallback without unbounded arithmetic.
  * Added `NaN` and `Inf` parameter validation to `PidAutotuner::begin()` and `step()`, preventing autotuner activation with corrupted sensor readings.
  * Expanded test suites: `test_pid.cpp` from 14 to 17 tests and `test_autotuner.cpp` from 14 to 16 tests (covering infinity safety, negative tuning rejection, and scheduler edge cases).
  * Synchronized `components/pid/README.md`.
* **Actuator Hardware & Time-Proportional Output Hardening (`components/output`)**:
  * Upgraded global time-window constants in `burst_fire.hpp` to `inline constexpr` for strict C++20 ODR safety.
  * Added `std::isinf` protection in `BurstFire::setPower()` to prevent infinite float inputs from commanding uncontrolled heating power.
  * Added `[[nodiscard]] bool OutputSystemConfig::validate() const` enforcing physical pin sanity, preventing pin collisions, and protecting ESP32-S3 Octal PSRAM GPIOs (35-42).
  * Hardened `OutputManager::setChannelPolarity()` to immediately refresh physical output levels upon runtime polarity changes.
  * Expanded `test_output.cpp` test suite from 11 to 15 unit tests (covering infinity float safety, config validation, hardware lifecycle initialization, and all-channel polarity reconfiguration).
  * Boosted source-level line coverage in `output_manager.cpp` from 63.9% to 89.3%, region coverage to 89.9%, and branch coverage to 84.4%.
  * Synchronized `components/output/README.md` test matrix.
* **Thermal & Hardware Safety Watchdog Hardening (`components/safety`)**:
  * Upgraded global safety configuration constants in `safety_watchdog.hpp` to `inline constexpr` for strict C++20 ODR compliance across translation units.
  * Added `[[nodiscard]] bool SafetyConfig::validate() const` and defensive input validation guard in `SafetyWatchdog::updateConfig()` rejecting NaN or logically inverted limits.
  * Hardened format specifier matching in `snprintf` for heater failure logging (`%u` with `static_cast<unsigned int>`) and added null pointer guard in `tripFault()`.
  * Symmetrically expanded the unit test suite in `test_safety.cpp` from 13 to 21 tests (adding bottom-channel undertemp, disconnect, IC hardware fault, stuck SSR, no-rise, NaN protection, sustained latched fault inhibit, and config validation).
  * Boosted source-level line coverage in `safety_watchdog.cpp` from 77.8% to 96.3%, region coverage to 92.9%, and branch coverage to 84.0%.
  * Synchronized `components/safety/README.md` test table to reflect all 21 test cases.
* **Configuration String Corrections (`components/config`)**:
  * Corrected temperature boundary error message strings in `machine_config.cpp` from `30..300°C` to `20..300°C` to strictly match `Limits::MIN_TEMPERATURE = 20.0f`.
* **Clean-Code Architecture & Driver Refactoring (`components/sensor`)**:
  * Refactored `MAX31856::read()` to 100% linear Guard Clauses, flattening nested `if/else` decision trees and eliminating redundant branch assignments.
  * Decoupled responsibilities into dedicated, testable private helper methods (`isPorCondition`, `handlePorRecovery`, `notifyPorRestoredIfPending`, `processFaultStreak`, `isTemperatureSane`, `updateEmaFilter`).
  * Deduplicated hardware boot-time POST SPI pattern toggle logic into a data-driven `std::array` loop.
  * Formally codified Clean-Code & Control-Flow guidelines in `AGENTS.md` (linear guard clauses, branch hoisting, edge-triggered logging, CCN $\le$ 8).
  * Synchronized `components/sensor/README.md` to fully document all 28 unit tests.
* **Zero-Warning MISRA C++:2023 & C++ Core Guidelines Compliance (`refactor(core)`)**:
  * Provided explicit constructors for `BurstfireTaskContext`, `WebTaskContext`, and `InputTaskContext` in `main/include/tasks/` to guarantee reference binding in member initialization lists (`cppcoreguidelines-pro-type-member-init`).
  * Enforced uniform value-initialization (`{}`) across domain aggregate structs (`ReflowProfile`, `PidLibrary`, `MachineSettings`, `SystemContextData`, `TelemetryData`).
  * Enforced logical const-correctness per MISRA C++:2023 Rule 10.3: Marked hardware-modifying setters (`setSsrTop`, `setSsrBottom`, `setFan`, `setLamp`, `setBuzzer`, `setInhibit`, `setSimulationMode`, `recordStepMarker`) non-const via `// NOLINTNEXTLINE(readability-make-member-function-const)`.
  * Adopted Modern C++20 algorithms & utilities: Replaced mixed-signed comparison with `std::cmp_less` in `QuickPID.cpp`, and updated PID library sorting to `std::ranges::sort` in `reflow_fsm.hpp`.
  * Marked `WifiManager::saveCredentialsToNvs()` as `const` with `[[nodiscard]]` and validated return status in callers.
* **Root Clang-Tidy Hardening (`.clang-tidy`, `tools/run_clang_tidy.py`)**:
  * Suppressed flawed `cppcoreguidelines-init-variables` (which violates C++ Core Guideline ES.20 on computed const initializers) and `readability-redundant-member-init` with documented architectural rationales.
  * Enhanced `run_clang_tidy.py` to strip GCC-specific cross-compiler flags, add picolibc/Xtensa system headers, and filter ESP-IDF vendor noise.
* **Documentation Synchronization Across All Subsystems**:
  * Synchronized total firmware unit test counts to 158 tests (145 on host) in `main/README.md`.
  * Corrected WebSocket telemetry broadcast period documentation to 2 Hz (500 ms) in `components/web/README.md` and `ws_handler.hpp`.

### Fixed

* **Hardware Boot-Time Route Test Isolation (`components/web`)**:
  * Gated mock-dependent HTTP static file route unit tests (tests 13–21 in `test_web.cpp`) behind `#ifndef ESP_PLATFORM`, eliminating the null-pointer `StoreProhibited` crash (`req.aux == nullptr` at `0x420780dd`) during boot-time hardware POST tests while maintaining full 222-test coverage on host builds.
* **WebSocket Connection Stability & HTTP Socket Saturation (`components/web`, `assets`)**:
  * Added explicit `Connection: close` response header on static web assets (`static_file_server.cpp`), captive portal probes, and JSON history endpoints, eliminating browser HTTP/1.1 Keep-Alive socket hoarding and keeping 9 of 10 socket slots immediately free for WebSocket and new requests.
  * Synchronized compile-time socket limit constant `MAX_HTTP_SOCKETS = 10` across `web_server.cpp` and `ws_handler.cpp`, fixing the buffer size mismatch in `httpd_get_client_list()` (`clientFds` size 8 vs `max_open_sockets` 10) that previously triggered `ESP_ERR_INVALID_ARG` and stalled telemetry broadcasts.
  * Re-enabled LRU socket purging (`lru_purge_enable = true`) with faster 2-second send/receive timeouts to immediately recycle stale HTTP sockets upon rapid browser refresh without rejecting new connections (`error in accept (128)`).
  * Added defensive early exit in `system_routes.cpp` (`getHistoryHandler()`) aborting chunked streaming upon client disconnect, eliminating bursts of 50+ `error in send : 128` socket warnings.
  * Hardened frontend WebSocket lifecycle in `assets/app.js`: registered `beforeunload` graceful socket teardown (`ws.close(1000)`), implemented 3-phase badge state machine with 1.5-second neutral grace period avoiding instant false-alarm red badges on page load, and capped reconnection backoff.
  * Added silent `204 No Content` handlers in `static_file_server.cpp` for background OS sync probes (`/time/`, `/upd/`, `/eupd/`), stopping connection retry storms and 404 log flooding from connected Apple and Windows devices.
* **Web UI Theme & Settings Stale Reversion on Tab Switches (`components/web`, `assets`)**:
  * Synchronized `ApiContext::settings` directly in RAM upon theme changes in `postThemeHandler` and updated `getSettingsHandler` and `getThemeHandler` to serve live configurations from RAM instead of reading stale flash files during active processes.
  * Implemented deferred settings flash persistence via Core 0 `web_task` (`pendingSettingsSave`), safely committing in-flight UI theme changes to Flash after an active process returns to IDLE.
* **Core 1 Flash I/O Elimination & Real-Time Isolation (`main`, `components/storage`)**:
  * Introduced `applySettingsToSubsystems()` in `AppController` and eliminated all blocking LittleFS flash reads on Core 1 in `control_task.cpp` (`DONE -> IDLE`, `PREHEAT`, `AUTOTUNE`, `RELOAD_SETTINGS`), guaranteeing 0 ms SPI bus jitter during PID control.
  * Implemented an in-memory Reflow Profile Cache (`_profileCache`) in `StorageManager`, serving profile lists and profiles directly from RAM without LittleFS disk reads during runtime tab switches and active heating runs.
* **Defensive Union Zeroing (`components/web`)**:
  * Added `std::memset(&wifi_config, 0, sizeof(wifi_config))` in `wifi_manager.cpp` ensuring all union bytes are safely cleared before passing to ESP-IDF Wi-Fi driver.
* **Dead Code & Allocation Optimizations (`components/web`)**:
  * Fixed unsigned boundary check `req->content_len <= 0` to `== 0` in `ota_routes.cpp`.
  * Replaced redundant heap reallocations in `static_file_server.cpp` with in-place `uriClean.resize(qPos)`.
* **Wi-Fi SoftAP Password Route Mismatch in Web UI (`components/web`, `assets/app.js`)**:
  * Resolved endpoint mismatch where the Settings UI tab called non-existent `POST /api/settings/wifi` instead of the canonical backend endpoint `POST /api/security/wifi`.
  * Unified `app.js` to use `POST /api/security/wifi` across both the settings tab and onboarding dialogs.

---

## [1.0.0-beta.4] - 2026-10-20

### Added

* **Zero-Warnings Policy & Compiler Hardening Matrix (`cmake/strict_warnings.cmake`)**:
  * Established a centralized hardening matrix (`target_enable_strict_warnings`) enforcing `-Wall -Wextra -Werror -Wconversion -Wshadow -Wnull-dereference -Wdouble-promotion -Wformat=2 -Woverloaded-virtual -Wnon-virtual-dtor -Wcast-align -Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wunused-result` and `-D_FORTIFY_SOURCE=2`.
  * Enforced `-Werror` across `main` and all 10 components (including `QuickPID-3.1.9_Library` and `sTune-2.4.0_Library`) with 100% clean builds.
* **Hardware Stack Smashing Protection (`sdkconfig.defaults`)**:
  * Activated `-fstack-protector-strong` via `CONFIG_COMPILER_STACK_CHECK_MODE_STRONG=y` and `CONFIG_STACK_CHECK_STRONG=y` in both application image and bootloader.
* **Clang-Tidy Static AST Analysis Setup (`.clang-tidy`)**:
  * Created project-wide `.clang-tidy` ruleset based on SEI CERT C++, MISRA C++:2023 principles, Bugprone, Modernize, Performance, and C++ Core Guidelines with automated `idf.py clang-check` execution.
* **Type-Safe JSON Configuration Parsing (`components/web`)**:
  * Refactored settings parsing in `rest_api.cpp` using type-safe lambda helpers (`parseBool`, `parseString`, `parseFloat`, `parseUint32`, `parseUint8`), eliminating `bugprone-assignment-in-if-condition` and unsafe C-style casts.
* **Integer Arithmetic & Narrowing Conversion Fixes (`components/sensor`)**:
  * Fixed Cold Junction 16-bit register assembly in `max31856.cpp` using well-defined `uint16_t` bitwise shifts and explicit `int16_t` casts.
  * Corrected unsigned sign-extension literals in `test_sensor.cpp`.
* **Zero Double-Promotion Hardening Across All Layers**:
  * Explicitly typed float parameters in `ESP_LOG` / `printf` calls across `app_controller.cpp`, `control_task.cpp`, `reflow_fsm.cpp`, `sTune.cpp`, `pid_autotuner.cpp`, `safety_watchdog.cpp`, `storage_manager.cpp`, `rest_api.cpp`, and `ws_handler.cpp` to prevent software FPU emulation penalties.

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
* **Configurable Passive/Natural Cooling (`components/config`, `components/fsm`, `assets`)**:
  * Lowered `MIN_FAN_DURATION_S` limit from 10s to 0s, allowing operators to disable the automatic cooling fan completely.
  * In passive cooling mode (`fanCoolingDurationS = 0`), the fan remains OFF during `COOLING`, allowing the PCB to cool naturally until reaching `coolingSafeTemp` before transitioning to `DONE` (manual fan override remains available at all times).
* **High-Visibility Test Failure Diagnostics (`main/CMakeLists.txt`, `main/src/test_runner.cpp`)**:
  * Implemented linker wrap interceptor (`-Wl,--wrap=UnityDefaultTestRun`) catching test failures in real-time across all 11 modules and emitting bold purple/magenta (`\033[1;35m`) failure markers (`>>> [UNIT TEST FAILED] <test_name> <<<`) directly beneath failing assertions.
  * Restored clean, uncolored white text for all passing tests to prevent terminal log clutter, and aligned boot test failure banners to pixel-perfect 60-character symmetry.
* **Modular REST API Routing Architecture (`components/web`)**:
  * Decoupled monolithic `rest_api.cpp` into dedicated route controllers (`ControlRoutes`, `ProfileRoutes`, `OtaRoutes`, `SystemRoutes`) sharing an encapsulated `ApiContext`.
* **Static Asset Cache-Busting & Long-Term Caching (`components/web`, `tools/pack_web.py`)**:
  * Implemented automated version-based query string injection (`?v=<version>`) during packaging for CSS, JS, and dynamically loaded language bundles (`/lang/de.js`).
  * Configured HTTP cache header `Cache-Control: public, max-age=604800, immutable` (7 days) for static assets while enforcing `no-cache, no-store, must-revalidate` for `index.html`.
* **Decoupled FSM Subsystems (`components/fsm`)**:
  * Extracted `StepRunner` (`step_runner.cpp` / `.hpp`) for discrete profile step tracking, progress calculation, and duration advancement.
  * Extracted `CoolingController` (`cooling_controller.cpp` / `.hpp`) for automated fan cooling curves and natural convection management.
* **Dedicated JSON Serialization Engine (`components/storage`)**:
  * Separated `JsonSerializer` (`json_serializer.cpp` / `.hpp`) from LittleFS file I/O `StorageManager`, isolating cJSON parsing, string generation, and domain mapping.
* **Thermal Physics Simulator Implementation (`components/simulation`)**:
  * Extracted `ThermalSimulator` implementation from header-only into `thermal_simulator.cpp`, incorporating IEEE 754 NaN/Inf input sanitization and automatic ambient temperature self-healing.
* **Decoupled PID Gain Scheduling (`components/pid`)**:
  * Isolated gain scheduling logic into `GainScheduler` (`gain_scheduler.hpp`) and decoupled autotuning pipeline.
* **Decoupled Task Context Architecture (`main`)**:
  * Implemented Principle of Least Privilege across dual-core FreeRTOS tasks: introduced dedicated context structures (`BurstfireTaskContext`, `InputTaskContext`, `WebTaskContext`) providing only strictly required subsystem pointers.
* **AI Agent Guidelines & Project Rule Enforcement (`AGENTS.md`)**:
  * Established persistent guidelines defining C++20 conventions, dual-license SPDX rules for third-party libraries, and toolchain verification requirements.
* **Automated Comment Hierarchy Linter (`tools/check_code_style.py`)**:
  * Integrated `readability-comment-style` check validating strict 79-character width for `// ===` section dividers and forbidding arbitrary dashed (`// ---`) divider lines.
* **Dynamic Cross-Platform Clang-Format Discovery (`tools/format_all.py`)**:
  * Replaced hardcoded paths with automatic discovery supporting system PATH, ESP-IDF tools directory (`~/.espressif/tools/esp-clang/`), Unix/Homebrew paths, and Windows environments.

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
* **Relaxed PID Library Validation for Empty State (`components/config`, `assets/app.js`)**:
  * Updated `PidLibrary::validate()` and frontend `validatePidLibraryPayload()` to allow empty point libraries, enabling operators to save empty libraries while the FSM cleanly falls back to `MachineSettings` baseline gains.
* **Standardized Code Comment Hierarchy Across All Components**:
  * Replaced all ad-hoc dashed divider lines (`// ---`) with clean, numbered step comments (`// 1.`, `// 2.`) or standard 80-column section dividers (`// ===...===`) across `app_controller.cpp`, `input_task.cpp`, `thermal_simulator.cpp`, and other units.
* **Asynchronous Parallel Delta-Sigma Priming (`components/sensor`, `main`)**:
  * Parallelized initial MAX31856 thermocouple conversions across Top and Bottom channels during POST bootstrap, eliminating ~160 ms of serial blocking delay at startup.
* **Modernized C++20 Callback Signatures & Atomic Semantics (`components/input`, `components/output`)**:
  * Standardized input callbacks on `const ButtonCallback&` / `const SwitchCallback&` to eliminate unnecessary `std::function` copies.
  * Employed `std::exchange` for atomic consume-on-read latch clearing in button edge detectors.
* **MAX31856 ESP32-S3 SPI Bus Timings & Brownout Resilience (`components/sensor`)**:
  * Configured hardware CS timing delays: `cs_ena_pretrans = 2` (2 SPI bit-cycles = 10 µs leading delay before SCLK) and `cs_ena_posttrans = 2` (2 SPI bit-cycles = 10 µs trailing delay after SCLK) at 200 kHz, safely exceeding the 100 ns datasheet setup/hold requirements ($t_\text{CSS}$, $t_\text{CSH}$) across all breakout board wiring variants.
  * Removed legacy `input_delay_ns = 80` (reset to 0 ns), which was an artificial workaround for classic ESP32 timing issues and unnecessary/inappropriate for the ESP32-S3 GPSPI bus at 200 kHz.
  * Added brownout recovery retry logic in `MAX31856::init()`: retries hardware initialization up to 3 times with 50 ms delays to allow internal voltage rails to stabilize on boards with marginal power supply decoupling.
  * Synchronized `components/sensor/README.md` to reflect these exact ESP32-S3 GPSPI timing specifications.
* **Dual MIT / AGPL-3.0-or-later SPDX Compliance**:
  * Verified and locked dual-license SPDX headers across `QuickPID-3.1.9_Library` and `sTune-2.4.0_Library` (`.cpp`, `.hpp`, and `CMakeLists.txt`).

### Fixed

* **Test Assertion Sync for Empty PID Library (`components/web/test/test_web.cpp`)**:
  * Synchronized unit test assertion in `test_web_domain_model_range_validations` with relaxed `PidLibrary::validate()` rules allowing empty point libraries.
* **Process Start Safety Interlocks (`components/fsm`, `components/web`, `main`)**:
  * Hardened `ReflowFSM::startPreheat()` and `startAutotune()` against starting when hardware outputs are inhibited.
  * Blocked Preheat, Reflow, and Autotune execution in `control_task` and REST API if `app->getSafety().hasFault()` is active.
  * Ensured `hasFault()` respects `enableSafetyWatchdog` bypass in `SafetyWatchdog`.
* **Obsolete ESP-IDF v6 Coredump Configuration Cleanup (`sdkconfig.defaults`)**:
  * Removed deprecated `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y` option; ESP-IDF v6.0.2 / v5.2+ retired the legacy binary coredump choice and produces ELF coredumps unconditionally when `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` is set.
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
* **Clang-Tidy Modernization & Math Parentheses in Third-Party Libraries (`components/QuickPID-3.1.9_Library`, `components/sTune-2.4.0_Library`)**:
  * Modernized constructors to `= default;` and eliminated redundant member initializers (`modernize-use-equals-default`).
  * Added explicit parentheses to mathematical formulas in `sTune::GetKi()`, `sTune::GetKd()`, and `QuickPID` to resolve `readability-math-missing-parentheses`.
* **Missing Source Registration in Simulation CMake (`components/simulation/CMakeLists.txt`, `README.md`)**:
  * Registered `thermal_simulator.cpp` under component `SRCS` and aligned architectural documentation after extraction from header-only.

### Known Issues

* **Wi-Fi SoftAP Password Modification in Web UI (`POST /api/security/wifi`)**:
  * Changing the Wi-Fi password through the Web UI Security settings tab does not work due to an endpoint mismatch in `app.js` (`POST /api/settings/wifi` called instead of `POST /api/security/wifi`). The factory default Wi-Fi password (`reflow123`) remains active.

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
