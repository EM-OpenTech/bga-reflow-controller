# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added

* **Comprehensive Dual-Core Task Watchdog Timer (TWDT)**:
  * Integrated explicit task watchdog registration (`esp_task_wdt_add`) and cyclic feeding (`esp_task_wdt_reset`) across all 5 FreeRTOS application tasks on Core 0 (`input_task`, `web_task`) and Core 1 (`safety_task`, `burstfire_task`, `control_task`).
  * Explicitly persisted TWDT configuration in `sdkconfig.defaults` (5-second timeout, Core 0 & Core 1 monitoring).
* **Hardware Cold-Junction Temperature Offset (`CJTO`)**:
  * Implemented MAX31856 register `0x09` (`CJTO`) writing in `sensor::MAX31856::writeThresholds()`.
  * Dynamically bound `topCjOffset` and `bottomCjOffset` from `MachineSettings` to both thermocouple ICs at boot and runtime reload.

### Changed

* **MAX31856 Atomic 6-Byte Burst Read**:
  * Optimized temperature, cold-junction, and fault status acquisition into a single atomic SPI burst transfer (`0x0A`..`0x0F`), eliminating data tearing and reducing SPI bus transaction overhead to $< 50\,\mu\text{s}$.
* **Hardware Register Limit Synchronization**:
  * Aligned cold-junction calibration offset boundaries in `machine_config.hpp`, `index.html`, and `app.js` to the physical 8-bit signed register limits of the MAX31856 ($-8.0^\circ\text{C} \dots +7.9^\circ\text{C}$).
* **Dynamic Constructor PID Initialization**:
  * Replaced hardcoded default PID parameters in `AppController` with central values from `config::MachineSettings`.
* **Frontend UI Palette Harmonization**:
  * Unified all green status elements (preheat-done blinking badge, status-log items, connection indicators) to the system Emerald palette (`#10b981` / `#34d399` / `rgba(16, 185, 129)`).

### Fixed

* **Transient EMV Spike & Sensor Fault Suppression**:
  * Added configurable `faultStreakLimit = 3` debouncing in `sensor::MAX31856` to prevent false-positive safety trips from single-cycle EMI noise.
* **Boot-Loop Safety on Test Failure**:
  * Added configurable `CONFIG_REFLOW_HALT_ON_TEST_FAILURE` guard preventing infinite reboot loops during development testing.
* **WebSocket Async Frame Handling on Disconnect**:
  * Hardened non-blocking client eviction and immediate socket session close upon client disconnect (`EAGAIN` / error 11).
* **Documentation & Task Count Alignment**:
  * Corrected outdated 4-task references to the actual 5-task dual-core architecture in header docstrings and comments.

### Documentation

* **Full Codebase Audit & SPDX Standardization**:
  * Standardized all 81 C++ source, header, and test files with official `AGPL-3.0-or-later` SPDX license headers.
  * Formatted 100% of files with uniform 80-column ASCII section banners (`// ============================================================================`).
  * Added complete Doxygen annotations (`@file`, `@brief`, `@copyright`, `@see`, `@param`, `@return`) across all modules.
* **Third-Party Notices**:
  * Corrected markdown table formatting and license texts in `THIRD-PARTY-NOTICES.md`.

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
