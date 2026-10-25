# BGA Reflow Controller - AI Agent Guidelines & Coding Standards

SPDX-FileCopyrightText: 2026 EM-OpenTech
SPDX-License-Identifier: AGPL-3.0-or-later

## 1. Project Overview & Environment
- **Platform:** ESP-IDF v6.0.2 / ESP32-S3
- **Language Standard:** C++20 (`-std=gnu++20`) — Project codebase strictly adheres to ISO C++20. ESP-IDF v6.0 compiles via GCC 15.2.0 (injecting `gnu++26` by default, making modern libstdc++ features available).
- **Compiler Warnings:** `-Wall -Wextra -Werror` (Zero warnings permitted)
- **Architecture:** Dual-Core FreeRTOS (Core 1: Real-Time PID & Safety, Core 0: Web & UI)
- **IDE & Toolchain Architecture (`.vscode` & Language Server):**
  - **`clangd` as Primary LSP:** In `.vscode/settings.json`, Microsoft IntelliSense is intentionally disabled (`"C_Cpp.intelliSenseEngine": "disabled"`, `"C_Cpp.errorSquiggles": "disabled"`). The high-performance `clangd` extension (via ESP-IDF Clang toolchain) is used exclusively with `.clang-tidy` integration to eliminate duplicate squiggle conflicts and provide live AST diagnostics matching GCC 15.2.0.
  - **Header Parsing via `c_cpp_properties.json`:** Set to `"cppStandard": "c++23"` so that the editor parser resolves modern GCC 15.2.0 standard library headers (such as `std::expected`) without false syntax squiggles.

---

## 2. Comment & Code Style Conventions

### Comment Hierarchy (Strict 3-Level Standard)
1. **Level 1: Major Section & File Dividers**
   - MUST use double-line equals signs `// ===...===`.
   - MUST be exactly **79 characters** wide (80-column standard with newline).
   - Outer / File-level (column 0):
     ```cpp
     // ============================================================================
     // Section Title
     // ============================================================================
     ```
   - Class / Indented (4 spaces):
     ```cpp
         // ========================================================================
         // Member Group Title
         // ========================================================================
     ```

2. **Level 2: Function-Internal Steps & Test Cases**
   - Use clean, numbered single-line comments:
     ```cpp
         // 1. Initialize sensor hardware
         // 2. Validate calibration parameters
     ```

3. **Level 3: Explanatory Inline Comments**
   - Use standard single-line comments for rationale and non-obvious logic:
     ```cpp
         // Pre-load loaded MachineSettings before begin() to prevent double-reconfig
     ```

### Forbidden Comment Styles
- **NO dashed divider lines:** Never use `// ---...` or `/* ---... */`.
- **NO custom divider lengths:** Never use arbitrary length bars like `// ======` (must be exactly 79 chars).
- **NO decorative ascii art:** Asterisk banners, hashes, or box borders inside functions are forbidden.

---

## 3. Modern C++20 & MISRA-Adjacent Safety Standards

### Type Safety & Conversions
- **Zero C-Style Casts:** Never use `(type)val`. Always use explicit C++ casts:
  - `static_cast<T>(val)` for value conversions, enum conversions, and arithmetic.
  - `reinterpret_cast<T>(val)` only when interfacing low-level hardware buffers.
- **Explicit Comparison on Bitwise Expressions:**
  - Always compare bitwise results explicitly: `if ((flags & Mask) != 0)` instead of `if (flags & Mask)`.
- **Boolean Simplification:**
  - Never compare booleans against literals: do not use `== true`, `== false`, `!= true`, or `? true : false`.

### Memory Safety & Resource Management (100% RAII)
- **Zero Raw Allocations:** `malloc()`, `calloc()`, `realloc()`, and raw `free()` are strictly forbidden in proprietary code.
- **Zero Unmanaged `new` / `delete`:** Dynamic heap objects must be owned by `std::unique_ptr` or standard containers.
- **Resource Handles via RAII:**
  - Files: Use `UniqueFile` (automatic `fclose`).
  - Directories: Use `UniqueDir` (automatic `closedir`).
  - cJSON structures: Use `UniqueCJson` (automatic `cJSON_Delete`).
- **Null Pointers:** Exclusively use `nullptr`. Legacy `NULL` is strictly forbidden in C++ code.

### String Safety & Logging
- **Forbidden Unsafe C-String Functions:** `strcpy`, `sprintf`, `strcat`, `gets`, and `strtok` are strictly forbidden.
  - Use `snprintf()`, `std::string`, or `std::string_view` with verified buffer bounds.
- **Format String Verification:**
  - The number and types of format specifiers (`%d`, `%u`, `%s`, `%f`, `%lu`, `%zu`) in `ESP_LOG*` / `printf` calls MUST exactly match the arguments passed. Mismatches cause undefined behavior and compiler errors under `-Werror=format`.

### Modern C++20 Idioms & Attributes
- **`[[nodiscard]]` on Getters & Validators:**
  - All pure getters, state queries (`hasFault()`, `isInhibited()`, `getState()`), and validation functions (`validate() const`) MUST be marked with `[[nodiscard]]`.
- **Global Constants:**
  - Configuration constants and system limits in headers MUST use `inline constexpr` (ODR-safe, single instance across translation units).
- **Container Emptiness:**
  - Always use `.empty()` instead of `.size() == 0` or `.length() == 0`.
- **Math Parenthesization:**
  - Mixed arithmetic operators (`+`, `-` with `*`, `/`, `%`) MUST be explicitly parenthesized to guarantee unambiguous operator precedence.

---

## 4. ESP32-S3 Hardware & Memory Constraints

### Octal PSRAM & DMA Buffer Isolation
- **Dedicated PSRAM Bus Pins:**
  - ESP32-S3 Octal PSRAM (OPI) occupies GPIO 35–42 permanently. Peripheral pin assignments (SPI, I2C, UART, SSRs, Relays) MUST never use or overlap with this range.
- **Internal SRAM Requirement for DMA:**
  - DMA hardware controllers (SPI master for MAX31856, Wi-Fi buffers, I2S) cannot safely perform transfers to/from external PSRAM on the ESP32-S3.
  - All DMA-capable buffers MUST be explicitly allocated in internal SRAM:
    ```cpp
    heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ```
- **Cache-Line Alignment:**
  - Octal DDR PSRAM requires 64-byte cache line alignment (`CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE_64B=y`).

### IRAM & Flash-Cache Stalls
- **SPI Master in IRAM:**
  - `CONFIG_SPI_MASTER_IN_IRAM=y` MUST be enabled in `sdkconfig.defaults`.
- **Interrupt Handler Placement:**
  - Time-critical ISR callbacks and SPI sensor transfer completion handlers MUST be placed in IRAM via the `IRAM_ATTR` macro. This guarantees uninterrupted 20 Hz / 100 Hz temperature sampling and burst-fire modulation even during concurrent LittleFS flash writes or NVS commits.

### Dual-Core Concurrency & Task Pinning
- **Core 1 (Deterministic Real-Time & Safety):**
  - Reserved strictly for real-time control:
    - `safety_task`: Priority 10, 20 Hz (50 ms cycle) – Safety monitoring & fault tripping
    - `burstfire_task`: Priority 8, 100 Hz (10 ms cycle) – SSR AC cycle modulation
    - `control_task`: Priority 7, 5 Hz (200 ms cycle, `config::Timing::CONTROL_LOOP_PERIOD_MS`) – Sensor acquisition, FSM tick, PID calculation
  - NEVER execute blocking I/O, flash filesystem writes, or network operations on Core 1.
- **Core 0 (Communication & Best-Effort I/O):**
  - Reserved for networking and background I/O:
    - `input_task`: Priority 6, 50 Hz (20 ms cycle) – Debounced button & encoder handling
    - `web_task`: Priority 5, 2 Hz (500 ms cycle) – WebSocket telemetry broadcasting
    - System background: lwIP TCP/IP stack and HTTP/WebSocket daemon
- **Synchronization Rules:**
  - Inter-core communication across Core 0 and Core 1 follows 4 architectural patterns:
    1. **Commands (Core 0 -> Core 1):** MUST use the FreeRTOS Command Queue (`g_fsmCmdQueue`) to prevent race conditions during state transitions.
    2. **Sensor & Actuator Snapshots (Core 1 Inter-Task):** MUST use the lock-free Single-Writer Double-Buffer mailbox with `std::atomic<uint8_t>` and acquire/release memory order (`publishSensorSnapshot()` / `getSensorSnapshot()`).
    3. **Fault Flags (Cross-Core Instant Query):** MUST use atomic types (`std::atomic<SafetyFault>` with relaxed/acquire-release ordering).
    4. **Telemetry & History Snapshots (Core 1 -> Core 0):** MUST use FreeRTOS mutexes with priority inheritance (`SystemContext::lock()` / `unlock()`, `configUSE_MUTEXES = 1`).
  - Heap allocations, logging (`ESP_LOG*`), and blocking operations inside critical sections or spinlocks are strictly forbidden.

---

## 5. Filesystem & Persistence Integrity (LittleFS)

### Atomic File Operations
- **Power-Loss Recovery Pattern:**
  - Profile configurations and machine settings MUST always be written to a temporary file (`.tmp`) first.
  - After flush and close, replace the destination file atomically via `rename()`:
    ```
    /littlefs/profiles/profile.json.tmp  -->  rename()  -->  /littlefs/profiles/profile.json
    ```
  - This eliminates flash corruption and zero-byte files if 230V mains power is abruptly lost during a write.

### Factory Profiles & Immutability
- **Factory Default Protection:**
  - The default factory profile (`factory-profile.json`) is immutable and protected.
  - `StorageManager::saveProfile()` and `StorageManager::deleteProfile()` MUST explicitly reject any attempt to overwrite or delete `factory-profile.json`.

### Path-Traversal & Input Sanitization
- **Strict Filename Validation:**
  - All profile filenames and REST URL parameters MUST be validated before filesystem access:
    - Reject any path containing directory traversal sequences (`..`).
    - Reject any path containing path separators (`/` or `\`).
    - Reject empty filenames.
    - Reject filenames exceeding `config::Limits::MAX_FILENAME_LENGTH` (40 characters).
- **Filesystem Lifecycle Pre-Checks:**
  - All storage methods MUST verify `_mounted == true` before performing LittleFS calls. Unmounted operations must return a defined error code (`false` / `ESP_ERR_INVALID_STATE`) without crashing.

---

## 6. SPDX & Licensing Rules
- All proprietary source/header files (`.cpp`, `.hpp`, `.h`, `.c`), scripts (`.py`), web assets (`.html`, `.js`, `.css`), and CMakeLists:
  ```cpp
  // SPDX-FileCopyrightText: 2026 EM-OpenTech
  // SPDX-License-Identifier: AGPL-3.0-or-later
  ```
- **Third-Party Libraries (`QuickPID` & `sTune`):**
  - MUST maintain their dual-license header:
    - QuickPID: `SPDX-FileCopyrightText: 2023 David Lloyd <dlloydev@testcor.ca>` + `MIT AND AGPL-3.0-or-later`
    - sTune: `SPDX-FileCopyrightText: 2022 Dlloydev <dlloydev@testcor.ca>` + `MIT AND AGPL-3.0-or-later`
  - NEVER change third-party libraries to pure AGPL.

---

## 7. Verification Workflow
Whenever editing C++ or header code, always run the local toolchain checks:
1. **Formatting:** `python3 tools/format_all.py` (formats all source & header files)
2. **Style & Static Analysis:** `python3 tools/check_code_style.py` (checks MISRA/Clang-Tidy + comment conventions across all files)
3. **Build:** `ninja -C build` (must compile cleanly with 0 warnings under `-Werror`)
4. **Sanitizer Host-Tests:** `python3 tools/run_host_tests.py` (executes 145+ unit tests across 10 modules under Clang AddressSanitizer & UndefinedBehaviorSanitizer)
5. **Multi-Core Concurrency Guard:** `python3 tools/run_tsan_test.py` (executes dual-core inter-thread stress test under Clang ThreadSanitizer)
6. **Code Coverage:** `python3 tools/run_coverage.py` (measures source-level line and branch coverage via LLVM)
7. **Clang-Tidy Core Guard:** `python3 tools/run_clang_tidy.py` (executes root .clang-tidy analysis across proprietary files)
8. **Cppcheck Secondary Guard:** `python3 tools/run_cppcheck.py` (executes secondary multi-core static analysis)

*Note:* When modifying Web UI frontend assets (`web/`), always run `python3 tools/pack_web.py` before compiling to update embedded gzip payloads.

---

## 8. Git Commit & Release Conventions

### Conventional Commits with Component Scopes
- Commit messages MUST adhere to the Conventional Commits specification:
  ```
  <type>(<scope>): <imperative summary in present tense>
  ```
- **Allowed Types:**
  - `feat`: New feature, UI capability, or hardware driver
  - `fix`: Bugfix, NaN protection, boundary clamp, or crash prevention
  - `refactor`: Clean-code, C++20 modernization, MISRA compliance without behavior changes
  - `build`: CMakeLists, compiler warning matrix, ESP-IDF sdkconfig, toolchains
  - `test`: Unit tests, mocks, sanitizer test suite, coverage additions
  - `docs`: READMEs, CHANGELOGs, licensing, API documentation
- **Component Scopes (MUST match directory structure):**
  - Single: `(sensor)`, `(safety)`, `(pid)`, `(output)`, `(fsm)`, `(input)`, `(storage)`, `(config)`, `(web)`, `(simulation)`, `(main)`, `(core)`
  - Multi-component (comma-separated): `(safety,web)`, `(ui,web,main)`
- **Pre-Release Tag Format:**
  - `Pre-Release: v1.0.0-beta.X - <high-level summary>`
