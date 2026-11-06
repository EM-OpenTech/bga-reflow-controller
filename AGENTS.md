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
- **`const` and `constexpr` Immutability:**
  - All variables, function parameters, references, and objects that are not modified after initialization MUST be marked `const` (or `constexpr` for compile-time constants). Immutability prevents unintended state mutation and enables compiler optimization.
- **Physical Units Suffixes:**
  - Variables, structure members, and constants representing physical, temporal, or spatial quantities MUST include an explicit unit suffix:
    - Time: `_MS`, `_US`, `_SEC` (or `_MIN`)
    - Temperature: `_C` (degrees Celsius)
    - Proportions: `_PERCENT`
    - Sizes: `_BYTES`, `_KB`
    - Frequencies: `_HZ`
  - Ambiguous bare identifiers (e.g. `timeout`, `window`, `rate`, `interval`, `temp`) without unit qualifiers are forbidden.
- **Floating-Point Invariant (`std::isfinite`):**
  - All floating-point calculations used in safety-critical paths (PID output, temperature tracking, slew-rate estimation) MUST be validated against non-finite IEEE 754 states (`NaN`, `+/-Inf`) using `std::isfinite()` prior to state persistence or actuator driving.
- **`[[nodiscard]]` on Getters & Validators:**
  - All pure getters, state queries (`hasFault()`, `isInhibited()`, `getState()`), and validation functions (`validate() const`) MUST be marked with `[[nodiscard]]`.
- **Global Constants:**
  - Configuration constants and system limits in headers MUST use `inline constexpr` (ODR-safe, single instance across translation units).
- **Container Emptiness:**
  - Always use `.empty()` instead of `.size() == 0` or `.length() == 0`.
- **Math Parenthesization:**
  - Mixed arithmetic operators (`+`, `-` with `*`, `/`, `%`) MUST be explicitly parenthesized to guarantee unambiguous operator precedence.

### Control Flow & Clean Code Architecture
- **Linear Guard Clauses over Nested `if/else` (Anti-"Arrow" Pattern):**
  - Deep nesting of `if/else` statements (> 2 levels) is strictly forbidden.
  - Functions MUST validate preconditions, error states, and bounds upfront using early-return Guard Clauses.
  - The successful execution path ("Happy Path") must remain at the root indentation level of the function body. Never wrap core logic inside a trailing `else` block.
- **Exhaustive `switch/case` over Enum Classes:**
  - `switch` statements over strongly-typed enums (`enum class`) MUST explicitly handle all enumerators.
  - Do NOT provide catch-all `default:` cases for enum switches unless handling untrusted external protocol data. Omitting `default:` allows the compiler (`-Wswitch`) to catch unhandled enumerators when new enum values are introduced.
- **Bounded Loops & Timeout Guards:**
  - Indefinite polling loops (`while (cond)`) on hardware registers, SPI ready lines, or FreeRTOS flags without a deterministic bound are strictly forbidden.
  - Loops polling external hardware MUST have an iteration counter cap or an explicit timeout check (e.g. `esp_timer_get_time()`).
- **Branch Deduplication & Code Hoisting:**
  - Statements executed identically in multiple conditional branches (e.g., state flags, cleanup, buffer resets) MUST NOT be duplicated. They must either be hoisted before the conditional check or encapsulated into a shared helper function.
  - Repetitive sequential hardware access patterns (such as boot POST pattern toggles) must be driven via iterations over `constexpr std::array` rather than copy-pasted blocks.
- **Edge-Triggered Logging in Cyclic Real-Time Tasks (Zero CLI Spam):**
  - Real-time tasks (Core 1: 5 Hz, 20 Hz, 100 Hz; Core 0: 50 Hz) must NEVER log error messages cyclically on every loop tick.
  - Strict 3-phase edge-triggered pattern:
    1. *Rising Edge (Fault Arrival):* Log exactly once at `ESP_LOGE` or `ESP_LOGW` and latch state (`_faultLogged = true`).
    2. *Sustained State (Fault Persists):* Execute silent retries/fallback in background (`verbose = false`), emitting ZERO terminal logs.
    3. *Falling Edge (Recovery):* Log exactly once at `ESP_LOGI` upon successful re-initialization and clear state (`_faultLogged = false`).
  - Distinct hardware fault domains (e.g., Level 1: IC bus/POR power loss vs. Level 2: sensor wire open-circuit) MUST maintain separate latch flags to prevent fault masking.
- **Single Responsibility Principle (SRP) & Complexity Caps:**
  - Complex orchestrator methods (e.g. `read()`, `tick()`, `process()`) must delegate specialized phases (hardware recovery, glitch filtering, sanity bounding, mathematical smoothing) into focused, named private helper methods.
  - Cyclomatic Complexity Number (CCN) per individual function should not exceed 8.
- **Strict Dead-Code Prohibition:**
  - Unused functions, unreferenced variables, dead prototypes, and commented-out code blocks MUST be purged completely. Source repositories rely on Git history for code retrieval, never lingering in-file code remnants.
- **Stop and Clarify on Ambiguities:**
  - When encountering conflicting architectural requirements, ambiguous domain semantics, or unverified safety invariants, AI agents and developers MUST stop execution and ask the user for clarification before making non-reversible assumptions.

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

### Hardware Fail-Safe & Flash Storage Lifecycle
- **Fail-Safe Hardware State on Reset/Panic:**
  - In a thermal system controlling 230V mains high-power heaters (top/bottom SSRs), the system MUST guarantee fail-safe de-energization upon any unhandled panic (`esp_panic`), assertion, brownout, or watchdog reset.
  - An emergency hardware shutdown handler MUST be registered via `esp_register_shutdown_handler()`. This callback directly forces SSR control GPIOs to LOW (0 V / inactive state) before the ESP32-S3 restarts.
- **Flash Write Prohibition during Heating Cycles:**
  - LittleFS flash filesystem writes (profiles, settings, calibration) suspend the flash cache and cause non-deterministic timing stalls.
  - Flash writes are strictly forbidden while the state machine is in active heating phases (`PREHEAT`, `SOAK`, `REFLOW`, `PEAK`). Filesystem mutations are permitted only in non-critical quiescent states (`IDLE`, `COOLING`, `COMPLETE`, `ERROR`).

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
4. **Sanitizer Host-Tests:** `python3 tools/run_host_tests.py` (executes 222+ unit tests across 12 test suites under Clang AddressSanitizer & UndefinedBehaviorSanitizer)
5. **Multi-Core Concurrency Guard:** `python3 tools/run_tsan_test.py` (executes dual-core inter-thread stress test under Clang ThreadSanitizer)
6. **Code Coverage:** `python3 tools/run_coverage.py` (measures source-level line and branch coverage via LLVM)
7. **Clang-Tidy Core Guard:** `python3 tools/run_clang_tidy.py` (executes root .clang-tidy analysis across proprietary files)
8. **Cppcheck Secondary Guard:** `python3 tools/run_cppcheck.py` (executes secondary multi-core static analysis)

*Note:* When modifying Web UI frontend assets (`assets/`), run `python3 tools/check_frontend.py` to validate web standards and `python3 tools/pack_web.py` before compiling to update embedded gzip payloads.

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

---

## 9. Frontend Architecture & Web Standards (HTML5, CSS3, ES2022+)

### Multi-Client & Headless Controller Architecture
- **Single Source of Truth (SSOT):**
  - The ESP32-S3 hardware controller is the sole authority for all process, hardware, and FSM states (`stateEnum`, `topTemp`, `bottomTemp`, `topPower`, `bottomPower`, `fan`, `lamp`, `elapsedSec`, `talSec`).
  - The controller operates completely autonomously ("headless"); reflow profiles execute safely without any connected browser.
  - The Web UI is strictly an interchangeable Human-Machine Interface (HMI). It holds no physical state and executes no autonomous control decisions.
- **Strict Separation of Global vs. Local State:**
  - *Global Controller State:* Shared across all connected clients via 2 Hz WebSocket telemetry.
  - *Local UI State:* Current active tab, form drafts, slider previews, and device theme (`reflow_theme` via `localStorage`). Different devices may browse different tabs independently.
- **Rogue Auto-Reconcile Prohibition:**
  - Frontends MUST NEVER silently reset or reconcile controller FSM states in the background (e.g. firing `exitBackup` or `stop`) simply because their local active tab does not match the active controller state.
  - When the controller enters an exclusive state (such as `BACKUP`), all clients must respect the state, display the lock indicator, and inhibit colliding actions without counter-attacking.
- **Deterministic Action Button Locking:**
  - Any button initiating a REST transaction (Save Profile, Save Settings, Update Wi-Fi, Autotune) MUST be immediately disabled (`btn.disabled = true`) with visual pending feedback, re-enabling only upon HTTP response resolution.

### Layered Dependency-Driven Module Hierarchy
To eliminate circular dependency crashes (`ReferenceError`) during build-time concatenation, frontend JavaScript follows a strict **5-layer dependency hierarchy** where data and calls flow strictly top-to-bottom:
1. **Layer 1: Foundation (Zero Dependencies):** `01_store.js` (State store), `02_i18n.js` (Localization), `03_utils.js` (Math, formatting, clamps).
2. **Layer 2: Transport & Base UI:** `04_api.js` (REST client & offline detection), `05_modal.js` (Alerts, confirms, prompts, toasts).
3. **Layer 3: Visualization & Telemetry Engine:** `06_chart.js` (Chart.js instance & datasets), `07_fsm.js` (Status badges & locks), `08_hud.js` (Live gauges & HUD updater), `09_ws.js` (WebSocket & watchdog).
4. **Layer 4: Domain Tab Controllers:** `10_tab_profiles.js` (Profiles CRUD), `11_tab_settings.js` (Machine settings & Wi-Fi), `12_tab_tunepid.js` (Autotune & PID table), `13_tab_backup.js` (Backup portal & restore).
5. **Layer 5: Bootstrap:** `14_app.js` (DOM caching, tab switching, lifecycle initialization).

### Decoupled CSS Module Architecture
Stylesheets are cleanly segregated into 7 domain modules in `assets/css/`:
- `01_variables.css`: Design tokens, CSS custom properties, Dark/Light palettes.
- `02_base.css`: CSS reset, box-sizing, typography, scrollbars, utility classes.
- `03_header.css`: 3-column navbar, brand, navigation pills, status badge, CLI log dropdown.
- `04_chart.css`: Chart container, live HUD gauges, control strip, lamp/fan glow.
- `05_tabs.css`: Shared tab layouts, accordions, form controls, tables (profile & PID).
- `06_backup.css`: Dedicated backup portal, orb hero stage, session cards, animations.
- `07_modal.css`: Modal dialogs (alert/confirm/prompt), backdrop, system info card.
- *Component-Scoped Responsive Rules:* Media queries (`@media (max-width: ...)`) MUST be maintained directly inside the respective component file, never in a trailing global dumping file.

### Defensive JavaScript Standards (ES2022+ & Clean Code)
- **Strict Mode & Scope:** All code MUST run in `"use strict";` and be scoped to prevent global namespace pollution.
- **Zero `var` & Immutability:** `var` is strictly forbidden. Use `const` by default for all references, objects, and immutable values; `let` only for counters or mutating flags.
- **Linear Guard Clauses (Anti-"Arrow" Pattern):**
  - Functions MUST validate preconditions, error boundaries, and disconnected states upfront with early returns.
  - Deep nesting (> 2 levels) is strictly forbidden. The happy path must stay at indentation level 0.
- **Single Responsibility & Helper Decoupling:**
  - Functions MUST focus on a single clear responsibility. Complex tasks (validation loops, step formatting, DOM synchronization) MUST be decoupled into named private helper functions to ensure human readability, modular testability, and future maintainability.
- **Strict Dead-Code & Export Hygiene:**
  - Unused functions, dead prototypes, orphaned variables, unreferenced DOM selectors, and dangling window bindings MUST be removed immediately. Global exports must contain strictly active, verified interfaces.
- **Deterministic Action Button Locking (Debounce):**
  - Buttons initiating asynchronous network or flash transactions (Save Settings, Save Profile, Autotune, Preheat) MUST be immediately disabled (`btn.disabled = true`) upon click and re-enabled only in a guaranteed `finally` block upon HTTP resolution. The emergency STOP button (`#btn-stop`) MUST remain strictly unlocked at all times.
- **Strict Equality & Numeric Validation:**
  - Always use `===` and `!==`. Loose equality (`==`, `!=`) is strictly forbidden.
  - All incoming telemetry numbers, temperature points, and PID gains MUST be validated against non-finite values (`NaN`, `+/-Inf`) using `Number.isFinite()` before passing to DOM or charts.
- **Physical Units Suffixes:**
  - Variables and constants representing physical, temporal, or spatial quantities MUST include an explicit unit suffix:
    - Time: `_ms`, `_us`, `_s` (or `_sec`)
    - Temperature: `_c` (or `tempC`)
    - Proportions: `_percent`
    - Sizes: `_bytes`, `_kb`
- **Emergency Stop Safety Invariant:**
  - The **STOP (Emergency Stop)** button MUST NEVER be disabled by telemetry timeouts or disconnect states. A click on STOP must immediately attempt a direct HTTP REST `POST /api/control {"action":"stop"}` to guarantee operator abort capability at all times.
- **Bounded Memory & Ring Buffers:**
  - Live chart telemetry arrays MUST be bounded to **14,400 data points** (4 hours at 1 Hz), allowing multiple complete reflow cycles without memory exhaustion or browser tab lag.
- **Resource Hygiene & Leak Prevention:**
  - All `setInterval` and `setTimeout` timers must be tracked and cleared via `clearInterval` / `clearTimeout` on reconnect or unmount.
  - Dynamic list elements MUST use event delegation on parent containers rather than attaching listeners in iterative loops.
  - Console logging in cyclic 2 Hz telemetry loops MUST follow edge-triggered reporting (zero console spam).
- **Asynchronous Safety & Unhandled Rejections:**
  - Every `await` invocation on network or storage APIs (`apiRequest`, `fetch`, file readers) MUST be wrapped in a `try/catch` block or chain a `.catch()` rejection handler.
  - Unhandled Promise Rejections are strictly prohibited.
- **Transactional Input Change Events (Flooding Protection):**
  - Continuous inputs (`<input type="range">`, numeric parameters) MUST dispatch REST mutations strictly on the `change` event (upon release / blur).
  - Dispatching network transactions on rapid `input` events is strictly prohibited to protect the ESP32 lwIP queue and flash wear.
- **Lifecycle Catch-Up & Screen Wake Lock:**
  - Upon device wake from sleep (`document.visibilityState === 'visible'`), the client MUST immediately re-verify WebSocket connectivity and fetch `/api/history` to backfill missing telemetry data.
  - While reflow is actively heating (`PREHEAT`, `SOAK`, `REFLOW`), the UI may request the Screen Wake Lock API (`navigator.wakeLock.request('screen')`) to prevent display timeout during critical process monitoring.

### Web Documentation & Comment Standards (JSDoc)
- **JSDoc for Public Interfaces:**
  - Exported functions, API methods, state handlers, and utility parsers MUST be documented using standard JSDoc:
    ```javascript
    /**
     * Serializes and dispatches updated machine settings to the controller.
     *
     * @param {Object} settings - Complete machine settings payload.
     * @returns {Promise<boolean>} True if successfully saved to NVS.
     */
    ```
- **Numbered Logical Steps:**
  - Function-internal steps MUST use numbered Level-2 comments (`// 1. Validate inputs`, `// 2. Dispatch payload`).
- **No C++ ASCII Banners in Web Assets:**
  - C++ 79-character equals banners (`// ===...===`) are reserved for backend code. Web assets MUST use clean JSDoc and standard CSS comment blocks (`/* ... */`).

### Offline-First & Zero-CDN Guarantee
- **Prohibition of External CDNs:** External CDN links (`fonts.googleapis.com`, `cdnjs`, `unpkg`, etc.) are STRICTLY FORBIDDEN.
- The web interface MUST load 100% autonomously, with zero delay and zero network timeouts on isolated local networks and soft APs (`192.168.4.1`).
- Typography MUST rely on the native system font stack: `-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif`.

### HTML Semantics, Accessibility & OWASP XSS Prevention
- **Semantic Tags & ID Prefixing:** Semantic HTML5 elements (`<header>`, `<main>`, `<nav>`, `<section>`, `<details>`) with standard ID prefixes: `btn-`, `set-`, `lbl-`, `panel-`.
- **Accessibility & Touch Targets:**
  - Interactive elements must support keyboard navigation (`role="tab"`, `aria-selected`, `Tab`, `Enter`).
  - Touch targets on mobile viewports MUST be at least **44 × 44 pixels** (WCAG 2.1 AA compliant).
  - All interactive controls (`button`, `input`, `select`, `.nav-tab-pill`) MUST declare `touch-action: manipulation;` to eliminate the 300 ms mobile tap delay and prevent accidental double-tap zoom gestures.
- **Form Integrity & Autofill Prevention:**
  - All machine-critical inputs (temperatures, times, PID gains, ramp rates) MUST explicitly declare `autocomplete="off"` and `autocorrect="off"` to prevent browser autofill from corrupting parameters with addresses or saved numbers.
  - Wi-Fi password fields MUST declare `autocomplete="new-password"`.
- **OWASP Client-Side XSS Prevention:**
  - Never interpolate dynamic strings into `innerHTML`. User-provided content (profile names, SSIDs, logs) MUST be assigned using `element.textContent` or `element.innerText`.
  - Dynamic string evaluation (`eval()`, `new Function()`) is strictly prohibited.

### Localization & Primary Language
- **English (`en`) as Primary Base Language:**
  - All standard HTML templates, embedded fallback strings, and default UI assets MUST be in English.
  - The controller boots and presents natively in English out-of-the-box.
- **German (`de`) as Localized Language Pack:**
  - German is maintained as a localized overlay in `assets/lang/de.js`.
  - All UI strings MUST be queried via `t('key', 'English Fallback')`.

### Build & Verification Workflow
- **Automated Frontend Auditing:** `python3 tools/check_frontend.py` validates web standards: scans for forbidden `var`, enforces strict equality, verifies complete I18n translation key coverage against `assets/lang/de.js`, bans external CDNs, and verifies flash ROM budget constraints (< 160 KB total gzipped assets).
- **Bundling & Gzip Compression:** `python3 tools/pack_web.py` concatenates CSS and JS modules according to their dependency order and compresses them with Level-9 Gzip into `components/web/build_assets/` (`style.css.gz`, `app.js.gz`).
- **Embedded Performance:** The ESP32 serves only 4 pre-gzipped static files (`index.html.gz`, `style.css.gz`, `app.js.gz`, `chart.min.js.gz`), maximizing memory efficiency and minimizing HTTP connection overhead.
