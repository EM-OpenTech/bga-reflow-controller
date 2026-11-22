# Main Application & System Orchestration (`main`)

Master application entry point, subsystem bootstrap, and dual-core FreeRTOS task coordinator for the **BGA Reflow Controller**, running natively on **ESP-IDF v6.0.2** and **FreeRTOS**.

The `main` component wires together all 11 modular firmware components, manages dependency injection, executes boot-time hardware diagnosis (Power-On Self-Test), and schedules the five real-time tasks across the dual-core Xtensa LX7 processor.

---

## 1. Overview & Architecture

The BGA Reflow Controller firmware operates on an **ESP32-S3-WROOM-1-N16R8** (240 MHz dual-core CPU, 16 MB SPI Flash, 8 MB Octal OPI PSRAM). The system architecture strictly decouples networking and user interaction from deterministic thermal control and hardware safety by binding tasks to dedicated CPU cores:

```
               +-----------------------------------------------------------+
               |                    main.cpp (app_main)                    |
               |  - Boot-Time Automated Test Suite (11 Components)         |
               |  - NVS Flash, Netif & System Event Loop Bootstrap         |
               |  - AppController Master Coordinator Instantiation         |
               +-----------------------------------------------------------+
                                             |
                                             v
               +-----------------------------------------------------------+
               |                    app::AppController                     |
               |  1. SystemContext Mutex & FSM Command Queue Setup         |
               |  2. LittleFS Storage Mounting & Settings Deserialization  |
               |  3. Hardware Outputs Initialization & Safe-State Bias     |
               |  4. Hardware Inputs Debounce Setup                        |
               |  5. Dual MAX31856 SPI2 Hardware POST & Direct Init        |
               |  5b. Power-On Self-Test (POST Diagnostics & Health Check) |
               |  6. Dual-Channel QuickPID & BurstFire Modulator Setup     |
               |  7. SafetyWatchdog Verification & Interlock Binding       |
               |  8. SoftAP Wi-Fi, mDNS (reflow.local) & WebServer Launch  |
               |  9. Spawn Dual-Core FreeRTOS Tasks                        |
               +-----------------------------------------------------------+
                                             |
                      +----------------------+----------------------+
                      |                                             |
                      v                                             v
+-------------------------------------------+ +-------------------------------------------+
|          CPU CORE 1: REAL-TIME & SAFETY   | |          CPU CORE 0: NETWORKING & UI      |
+-------------------------------------------+ +-------------------------------------------+
| Priority 10: safety_task (20 Hz / 50 ms)  | | Priority 6: input_task (50 Hz / 20 ms)    |
| - MAX31856 IC Register Faults (0x0F)      | | - Debounces Start/Stop buttons            |
| - Absolute Over/Undertemperature Limits   | | - Evaluates Short (<2s) / Long (>2s) Hold |
| - Stuck SSR & Heater Failure (No-Rise)    | | - Dispatches SW_FAN & SW_LAMP switches    |
| - Hardware Inhibit (OutputManager)        | | - Posts FsmCommand to Core 1 Queue        |
|                                           | |                                           |
| Priority 8: burstfire_task (100 Hz / 10ms)| | Priority 5: web_task (2 Hz / 500 ms)      |
| - Advances 10ms PWM Window Accumulators   | | - Reads Atomic SystemContext Snapshot     |
| - Zero-Drift Solid-State Relay Gate Drive | | - Streams JSON Telemetry to WebSocket     |
|                                           | | - Asynchronous HTTP 1.1 Web Server        |
| Priority 7: control_task (5 Hz / 200 ms)  | | - Captive Portal DNS Responder (UDP 53)   |
| - SPI Master: Samples Top & Bottom TC     | +-------------------------------------------+
| - Single-Writer: Updates Atomic Mailbox   |
| - Drains Core 0 FsmCommand Queue          |
| - Steps ReflowFSM Process Engine          |
| - Computes Top & Bottom QuickPID Output   |
| - Syncs Thread-Safe SystemContext         |
+-------------------------------------------+
```

---

## 2. Firmware Subsystem Topology & Inter-Component Wiring

The firmware is divided into 11 specialized components, all instantiated and coordinated by `AppController`:

```
                                  [ main::AppController ]
                                             |
     +---------------+---------------+-------+-------+---------------+---------------+
     |               |               |               |               |               |
     v               v               v               v               v               v
 [ config ]     [ storage ]     [ output ]       [ input ]       [ sensor ]       [ pid ]
  - Limits       - LittleFS      - OutputManager  - InputManager  - Dual MAX31856 - QuickPID (Top)
  - Pins         - Settings      - BurstFire(Top) - Buttons       - SPI2 Host     - QuickPID (Bot)
  - Timings      - Profiles      - BurstFire(Bot) - Switches      - Hardware CJC  - sTune Engine
     |               |               |               |               |               |
     +---------------+---------------+-------+-------+---------------+---------------+
                                             |
                     +-----------------------+-----------------------+
                     |                       |                       |
                     v                       v                       v
                [ safety ]                [ fsm ]                 [ web ]
             - SafetyWatchdog         - ReflowFSM             - WebServer
             - Stuck-SSR Detect       - Dual StepRunners      - RestApi (Core 0)
             - No-Rise Detect         - Sac305 Reference      - WebSocket (2 Hz)
             - Immediate Inhibit      - Dynamic PID Tuning    - WifiManager (SoftAP)
```

| Component | Responsibility in Firmware | Communication with `main` |
| :--- | :--- | :--- |
| **`config`** | Central compile-time limits, hardware pinouts, timing constants, and schema versions. | Included globally by all modules; zero runtime overhead. |
| **`storage`** | LittleFS file persistence, wear-leveling quotas, atomic staging, and schema migration. | Injected into `WebServer` for REST API profile and setting CRUD operations. |
| **`output`** | GPIO actuator HAL, polarity inversion, zero-crossing burst-fire PWM, and safety lockout. | Updated by `burstfire_task` (100 Hz); locked by `safety_task` on emergency. |
| **`input`** | 50 Hz hardware switch debouncing, long-press detection, and manual toggle overrides. | Polled by `input_task` (Core 0); posts validated actions to `g_fsmCmdQueue`. |
| **`sensor`** | Maxim/ADI MAX31856 SPI driver, cold junction compensation, EMA filters, and POR recovery. | Read exclusively by `control_task` on Core 1 (Single-Writer architecture). |
| **`pid`** | C++20 port of QuickPID (P-on-M / P-on-E) and sTune frequency-response autotuner. | Computed synchronously in `control_task` at 5 Hz (200 ms). |
| **`safety`** | Hard real-time thermal supervisor monitoring IC faults, limits, stuck SSRs, and no-rise. | Evaluated in `safety_task` at 20 Hz (Core 1); directly drives `OutputManager::setInhibit`. |
| **`fsm`** | Hierarchical reflow state machine managing dual-channel steps, soak, reflow, and cooling. | Stepped by `control_task` at 5 Hz; receives asynchronous commands via queue. |
| **`web`** | Asynchronous HTTP 1.1 server, WebSocket telemetry, ROM assets, captive DNS, and OTA. | Runs on Core 0; consumes lock-free telemetry from `SystemContext`. |
| **`simulation`**| Dual-channel lumped-parameter thermal simulator with ambient coupling and noise modeling. | Activated dynamically in `control_task` when `settings.simulationMode == true`. |

---

## 3. Concurrency Model & Cross-Core Data Exchange

To guarantee zero priority inversion and eliminate deadlocks on the dual-core processor, communication between Core 0 and Core 1 uses three specialized mechanisms:

### 3.1 Asynchronous Command Queue (`g_fsmCmdQueue`)
The Web REST API (Core 0) and physical input task (Core 0) **never invoke FSM methods directly**. Calling FSM methods from Core 0 while Core 1 executes `fsm.update()` would cause data races on `StepRunner` state.
* Core 0 posts a POD `FsmCommand` struct into `g_fsmCmdQueue` (`xQueueSend`).
* Core 1 drains the queue at the start of each 5 Hz cycle before running PID computations. On `RESET_FAULT`, both the FSM and `SafetyWatchdog` are reset synchronously on Core 1, releasing hardware SSR inhibit for both Web UI and physical STOP button actions.

### 3.2 Lock-Free Double-Buffered Sensor Mailbox
`control_task` is the sole SPI master accessing the physical MAX31856 sensors. To share live readings with `safety_task` (Core 1) and `web_task` (Core 0) without spinlocks or mutex stalls:
* `AppController` maintains two `SensorSnapshot` buffers.
* `publishSensorSnapshot()` writes to the inactive buffer and flips an `std::atomic<uint8_t>` index with `std::memory_order_release`.
* Readers call `getSensorSnapshot()` with `std::memory_order_acquire`, retrieving a consistent snapshot without blocking.

### 3.3 Thread-Safe Telemetry Context (`SystemContext`)
A shared `SystemContext` coordinates UI display variables, step markers, profile progress, and execution history across cores:
* **Decoupled Scalar Snapshot (`SystemContextData`):** Contains only lightweight scalar variables (temperatures, powers, state enum, flags, step markers, and profile strings). The 2 Hz `getSnapshot()` call used by Core 0 (`web_task`) copies solely this small struct, executing with **zero heap allocations and zero memory copying overhead**.
* **Isolated Long-Term History (`_history`):** The 10,800-point (3-hour @ 1 Hz) history buffer is decoupled from the 2 Hz snapshot and stored in `_history`. Pre-allocated at boot via `_history.reserve(config::Limits::MAX_HISTORY_POINTS)` directly into 8 MB Octal PSRAM (as 216 KB > 64 KB `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=65536`). Access is protected via `lock(10)` and queried exclusively by `control_task` (for sample appending) and `/api/history` (for chunked REST streaming). This architectural separation completely eliminates heap fragmentation and Task Watchdog (TWDT) starvation during long reflow runs.

---

## 4. Boot Sequence & Power-On Self-Test (POST)

When power is applied, `app_main()` and `AppController::begin()` coordinate an orderly initialization pipeline:

```
[ Power On ]
     |
     v
[ Phase 0: Automated Test Suite ]    ---> (Optional boot-time verification of all 11 modules)
     |
     v
[ Phase 1: Context & FSM Queue ]     ---> Initializes SystemContext mutex & Core 0->Core 1 queue
     |
     v
[ Phase 2: LittleFS & Settings ]     ---> Mounts /littlefs, loads settings, profiles & PID library
     |
     v
[ Phase 3: Hardware Actuators ]      ---> Configures GPIOs, enforces safe de-energized default (0V)
     |
     v
[ Phase 4: Hardware Inputs ]         ---> Configures debounced Start/Stop buttons and override switches
     |
     v
[ Phase 5: Temperature Sensors ]     ---> SPI2 polling setup (DMA disabled), preloads CJTO & settings, primes ICs
     |
     v
[ Phase 5b: Power-On Self-Test ]     ---> Hardware diagnosis (Register toggle POST, brownout cause, memory, sensors)
     |
     v
[ Phase 6: Control Loops & Safety ]  ---> QuickPID tunings, BurstFire windows & Safety Watchdog
     |
     v
[ Phase 7: Networking & Server ]    ---> Starts SoftAP Wi-Fi, mDNS (reflow.local), and HTTP daemon
     |
     v
[ Phase 8: Task Dispatcher ]        ---> Spawns 5 FreeRTOS tasks pinned to Core 0 and Core 1
```

### Self-Healing & Safety Rollback Protection
* **Brownout / WDT Post-Mortem:** If the previous boot was caused by a Task Watchdog or brownout, a persistent warning flag is registered in the POST report and displayed on the Web UI.
* **OTA Firmware Validation:** On successful initialization, `esp_ota_mark_app_valid_cancel_rollback()` is invoked. If new firmware crashes or hangs during boot, the ESP-IDF bootloader automatically rolls back to the previous stable partition.

---

## 5. FreeRTOS Task Execution Schedule

All tasks register with the **ESP-IDF Task Watchdog Timer (TWDT)** (5-second timeout). If any task locks or starves for longer than 5 seconds, an automatic diagnostic reset is triggered:

| Task Name | Core | Frequency | Priority | Stack | Functionality |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **`safety_task`** | 1 | 20 Hz (50 ms) | **10** (Highest) | 4096 B | Real-time hazard detection, stuck SSR / no-rise monitoring, hardware inhibit. |
| **`burstfire_task`**| 1 | 100 Hz (10 ms) | **8** | 4096 B | Zero-crossing time-proportional SSR PWM gate switching with zero drift. |
| **`control_task`** | 1 | 5 Hz (200 ms) | **7** | 8192 B | Synchronous control loop: sensor sampling, FSM state engine, PID calculations. |
| **`input_task`** | 0 | 50 Hz (20 ms) | **6** | 4096 B | Physical GPIO button debouncing, long-press detection, master switch overrides. |
| **`web_task`** | 0 | 2 Hz (500 ms) | **5** | 8192 B | Periodic WebSocket live telemetry frame broadcasting to connected browsers. |

### 5.1 Decoupled Task Context Architecture (Principle of Least Privilege)

To enforce modular encapsulation and prevent unnecessary coupling with monolithic controller state, background tasks do not receive the full `AppController&`. Instead, dedicated, strongly-typed context structures inject only the minimal set of dependencies required for each task:

* **`BurstfireTaskContext`** (`burstfire_task`): Injects solely `topBurst`, `bottomBurst`, and `outputs` for high-frequency (100 Hz) SSR PWM switching.
* **`InputTaskContext`** (`input_task`): Injects debounced `inputs`, `safety` supervisor, `storage`, `context`, and `fsm` for button/switch events and profile triggering.
* **`WebTaskContext`** (`web_task`): Injects only `SystemContext` and `webServer` for periodic WebSocket telemetry streaming.
* **`AppController*`** (`control_task` & `safety_task`): Retained exclusively for the central control loop and real-time safety supervisor requiring holistic system coordination and atomic mailbox updates.

---

## 6. Unit Testing & Verification

The firmware features an automated, boot-time verification framework executed during **Phase 0** before hardware peripherals and control tasks are activated. Test execution is governed by compile-time configuration (`CONFIG_REFLOW_ENABLE_UNIT_TESTS`).

### 6.1 Unified Boot-Time Test Runner (`main/src/test_runner.cpp`)

The master test runner `app::runAllUnitTests()` orchestrates all 12 test suites across the 11 firmware components, executing a total of **235 automated unit and integration tests** (all 235 tests natively executable on host CPU under Clang ASan/UBSan):

| # | Subsystem Test Runner | Module / Test File | Coverage Focus |
|---|:---|:---|:---|
| 1 | `run_config_tests()` | `components/config/test/test_config.cpp` | Limits, defaults, JSON schema boundary validations, NaN/Inf rejection. |
| 2 | `run_simulation_tests()` | `components/simulation/test/test_simulation.cpp` | Physics model, thermal transfer, ambient clamping, NaN sanitization. |
| 3 | `run_output_tests()` | `components/output/test/test_output.cpp` | GPIO HAL, polarity inversion, burst-fire PWM, safety inhibit latching. |
| 4 | `run_pid_tests()` | `components/pid/test/test_pid.cpp` | QuickPID C++20 port, P-on-M / P-on-E, derivative kicks, integral windup. |
| 5 | `run_autotuner_tests()` | `components/pid/test/test_autotuner.cpp` | `sTune` inflection-point analysis, gain derivation, oscillation guards. |
| 6 | `run_safety_tests()` | `components/safety/test/test_safety.cpp` | IC faults, overtemp, stuck SSR inversion, no-rise detection, bypass modes. |
| 7 | `run_sensor_tests()` | `components/sensor/test/test_sensor.cpp` | MAX31856 SPI2 transactions, cold-junction math, EMA filtering, POR recovery. |
| 8 | `run_input_tests()` | `components/input/test/test_input.cpp` | Switch debouncing, short/long-press timing, atomic consume-on-read latches. |
| 9 | `run_storage_tests()` | `components/storage/test/test_storage.cpp` | LittleFS I/O, wear-leveling quotas, atomic staging, profile write protection. |
| 10 | `run_fsm_tests()` | `components/fsm/test/test_fsm.cpp` | StepRunner, state transitions, Soak/Reflow ramp rates, passive cooling. |
| 11 | `run_web_tests()` | `components/web/test/test_web.cpp` | REST API routes, JSON parsing, empty PID library handling, WebSocket frames. |
| 12 | `run_main_integration_tests()` | `main/test/test_main_integration.cpp` | SystemContext atomic locking, cross-core FSM queue, TWDT lifecycle. |

### 6.2 High-Visibility Failure Diagnostics via Linker Wrap (`-Wl,--wrap=UnityDefaultTestRun`)

To eliminate the need to manually parse thousands of serial log lines for `:FAIL` assertions, the build system hooks directly into Unity's test execution dispatcher:
1. **Linker Wrapping:** `main/CMakeLists.txt` links with `-Wl,--wrap=UnityDefaultTestRun`. The linker redirects Unity's internal test execution through `__wrap_UnityDefaultTestRun()`.
2. **Real-Time Failure Marker:** If a test assertion fails (`Unity.TestFailures > failBefore`), the wrapper immediately outputs a high-visibility bold purple/magenta marker:
   ```text
   \033[1;35m>>> [UNIT TEST FAILED] <test_function_name> <<<\033[0m
   ```
   directly beneath the failed assertion in the serial log.
3. **Clean Output Architecture:** Standard passing tests (`:PASS`) remain uncolored white text, keeping serial logs readable and preventing color conflicts with ESP-IDF system logging (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`).
4. **Symmetrical Summary Banner:** If any test fails, an eye-catching 60-character summary banner is printed before returning control to the boot pipeline:
   ```text
   ############################################################
   ###  BOOT TEST FAILURE:  1 UNIT TEST(S) FAILED!          ###
   ###  PLEASE CHECK THE ':FAIL' OUTPUT ABOVE FOR DETAILS!  ###
   ############################################################
   ```

### 6.3 Main Integration Test Suite (`test_main_integration.cpp`)

Integration tests verify multi-component contracts, thread-safe context locking, and boot watchdog lifecycles:

| # | Test Function | Verified Behavior |
|---|:---|:---|
| 1 | `test_main_system_context_init` | Verifies default state, zeroed temperatures, and empty telemetry containers on boot. |
| 2 | `test_main_system_context_lock_mutation` | Verifies recursive mutex locking, data mutations, and consistent atomic snapshots. |
| 3 | `test_main_firmware_version_descriptor` | Validates firmware application descriptor (`esp_app_desc_t`) magic bytes and project name. |
| 4 | `test_main_fsm_command_queue_operations` | Validates cross-core FreeRTOS queue posting, draining, and heap profile pointer safety. |
| 5 | `test_main_settings_parameter_binding` | Validates `MachineSettings` parameter binding, boundary ranges, and sensor offset validation. |
| 6 | `test_main_task_watchdog_lifecycle` | Validates 5-second TWDT configuration, dynamic subscription, feeding, and unsubscribing. |
| 7 | `test_main_fsm_command_queue_edge_cases` | Validates command queue boundary conditions, null payload pointers, and queue overflow rejection. |
| 8 | `test_main_system_context_edge_cases` | Validates recursive context mutex timeouts and thread-safety edge cases. |

---

## 7. CMake Integration

Defined in [`CMakeLists.txt`](CMakeLists.txt):

```cmake
set(TEST_SRCS "")

if(CONFIG_REFLOW_ENABLE_UNIT_TESTS)
    list(APPEND TEST_SRCS
        "src/test_runner.cpp"
        "test/test_main_integration.cpp"
        "../components/config/test/test_config.cpp"
        "../components/simulation/test/test_simulation.cpp"
        "../components/output/test/test_output.cpp"
        "../components/pid/test/test_pid.cpp"
        "../components/pid/test/test_autotuner.cpp"
        "../components/safety/test/test_safety.cpp"
        "../components/sensor/test/test_sensor.cpp"
        "../components/input/test/test_input.cpp"
        "../components/storage/test/test_storage.cpp"
        "../components/fsm/test/test_fsm.cpp"
        "../components/web/test/test_web.cpp"
    )
endif()

idf_component_register(
    SRCS 
        "main.cpp"
        "src/app_controller.cpp"
        "src/fsm_command_queue.cpp"
        "src/system_context.cpp"
        "src/tasks/safety_task.cpp"
        "src/tasks/burstfire_task.cpp"
        "src/tasks/control_task.cpp"
        "src/tasks/input_task.cpp"
        "src/tasks/web_task.cpp"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        config
        storage
        output
        input
        sensor
        pid
        safety
        fsm
        web
        simulation
        unity
        nvs_flash
        esp_netif
        esp_event
        esp_timer
        freertos
        log
        driver
        app_update
        esp_app_format
)

if(CONFIG_REFLOW_ENABLE_UNIT_TESTS)
    target_sources(${COMPONENT_LIB} PRIVATE ${TEST_SRCS})
    target_link_libraries(${COMPONENT_LIB} PRIVATE "-Wl,--wrap=UnityDefaultTestRun")
endif()

target_enable_strict_warnings(${COMPONENT_LIB})
```

---

## 8. 📜 License

Copyright (C) 2026 EM-OpenTech

This component is part of the BGA Reflow Controller project and is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)**.

* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `AGPL-3.0-or-later`

See the project root [`LICENSE`](../LICENSE) file for the full license text.
