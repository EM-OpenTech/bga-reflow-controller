# Configuration & Domain Models Component (C++20)

Central Single Source of Truth (SSOT) configuration repository, domain data models, hardware pin mappings, and centralized validation engine for the **BGA Reflow Controller**, running natively on **ESP-IDF v6.0.2** and **FreeRTOS**.

This component provides a centralized, decoupled domain layer that defines all operational boundaries, hardware allocations, timing synchronizations, and data schemas across the embedded firmware, FreeRTOS tasks, REST API, LittleFS persistent storage, and Web UI.

---

## 1. Overview & Architecture

In safety-critical thermal systems, inconsistent limits, mismatched pin definitions, or conflicting timing periods across different modules can lead to thermal runaway, hardware damage, or system instability. The `config` component acts as the **Domain Layer Single Source of Truth (SSOT)**:

```
                                    +-----------------------------------------+
                                    |          config::Limits & Timing        |
                                    |  - Physical & Safety Limits             |
                                    |  - 200 ms Control Loop Synchronization  |
                                    |  - Schema Versions & Resolutions        |
                                    +-----------------------------------------+
                                                         |
         +-----------------------------------------------+-----------------------------------------------+
         |                                               |                                               |
         v                                               v                                               v
+------------------+                           +-------------------+                           +--------------------+
| config::PinConfig|                           | config::Domain    |                           | config::Validation |
| Hardware GPIO    |                           | - MachineSettings |                           | - Boundary Checks  |
| - SPI Bus (31856)|                           | - ReflowProfile   |                           | - Path Traversal   |
| - SSR Actuators  |                           | - PidLibrary      |                           | - Array Capacities |
| - Aux Actuators  |                           | - ProfileStep     |                           | - Result Reporting |
| - Tactile Inputs |                           | - PidPoint        |                           |                    |
+------------------+                           +-------------------+                           +--------------------+
         |                                               |                                               |
         +-----------------------------------------------+-----------------------------------------------+
                                                         |
                   +-------------------------------------+-------------------------------------+
                   |                                                                           |
                   v                                                                           v
    [ Embedded System Core ]                                                    [ Storage & Communication ]
    - sensor::MAX31856       (SPI pins, CJTO offsets, EMA alpha)                - storage::StorageManager   (Serialization)
    - output::BurstFire      (SSR pins, burst window bounds)                    - web_server::RestApi       (Validation)
    - output::OutputManager  (Fan, Lamp, Buzzer pins & timings)                 - Web UI Dashboard          (Sync Limits)
    - input::InputManager    (Start/Stop, Switch pins)                          - simulation::ThermalModel  (Sync Timings)
    - safety::SafetyWatchdog (Max temp, Stuck SSR, No-Rise)
    - fsm::ReflowFSM         (Profile steps, hold gate, settle)
    - pid::PIDController     (Gain scheduling points, gains)
```

---

## 2. Central Constants & Namespaces

The component organizes all system-wide parameters into dedicated namespaces within `config`:

### 2.1 `config::Limits`
Defines the strict operational boundaries enforced across the entire system:

| Parameter | Type / Value | Description |
| :--- | :---: | :--- |
| `MAX_NAME_LENGTH` | `30` chars | Maximum profile name length |
| `MAX_FILENAME_LENGTH` | `40` chars | Maximum storage filename length (including `.json`) |
| `MAX_PROFILE_STEPS` | `10` steps | Maximum heating steps per channel in a Reflow Profile |
| `MAX_PID_POINTS` | `15` points | Maximum temperature points per channel in PID Library |
| `MAX_HISTORY_POINTS` | `10800` samples | Maximum telemetry history buffer (3 hours at 1 Hz) |
| `MAX_JSON_PAYLOAD_BYTES`| `65536` bytes | Maximum JSON payload buffer size (64 KB) |
| `MAX_ZIP_PAYLOAD_BYTES` | `524288` bytes | Maximum ZIP backup package size (512 KB) |
| `MIN_TEMPERATURE` | `30.0` °C | Absolute minimum allowable target setpoint |
| `MAX_TEMPERATURE` | `300.0` °C | Absolute maximum allowable target setpoint |
| `MIN_SAFE_COOLING_TEMP` | `20.0` °C | Minimum allowable safe-to-touch threshold |
| `MAX_SAFE_COOLING_TEMP` | `100.0` °C | Maximum allowable safe-to-touch threshold |
| `MIN_RAMP_RATE` / `MAX` | `0.1` .. `10.0` °C/s | Heating ramp rate boundaries |
| `MAX_STEP_TIME_S` | `600` s | Maximum hold duration per step (10 minutes) |
| `MIN_BURST_WINDOW_MS` / `MAX` | `500` .. `4000` ms | SSR burst-fire time proportional PWM window |
| `MIN_PID_KP` / `MAX` | `0.0` .. `100.0` | Proportional gain boundaries |
| `MIN_PID_KI` / `MAX` | `0.0` .. `10.0` | Integral gain boundaries |
| `MIN_PID_KD` / `MAX` | `0.0` .. `100.0` | Derivative gain boundaries |
| `MIN_CJ_OFFSET` / `MAX` | `-8.0` .. `+7.9` °C | MAX31856 cold-junction calibration offset (`CJTO` hardware range) |
| `MIN_EMA_ALPHA` / `MAX` | `0.01` .. `1.0` | Sensor EMA filter smoothing factor range |

### 2.2 `config::Resolution`
Defines step sizes and decimal precision for user input and REST API validations:

| Category | Constant | Resolution | Unit |
| :--- | :--- | :---: | :---: |
| **Temperature** | `Resolution::TEMPERATURE` | `0.1` | °C |
| **Ramp Rate** | `Resolution::RAMP_RATE` | `0.01` | °C/s |
| **PID Gains** | `Resolution::PID_GAIN_KP`, `KD` | `0.01` | – |
| **PID Integral**| `Resolution::PID_GAIN_KI` | `0.001` | – |
| **EMA Filter** | `Resolution::FILTER_ALPHA` | `0.001` | – |
| **Calibration** | `Resolution::OFFSET_TEMP` | `0.1` | °C |
| **Timing** | `Resolution::TIME_SEC` / `TIME_MS` | `1` / `100` | s / ms |

### 2.3 `config::Timing`
Synchronizes multi-core task scheduling, peripheral sampling, and control loops:

```cpp
namespace Timing {
    constexpr uint32_t CONTROL_LOOP_PERIOD_MS = 200; // 5 Hz (200 ms)
}
```

> [!NOTE]
> **Harmonized Loop Synchronization:**  
> The 200 ms (5 Hz) period harmonizes:
> 1. **MAX31856 ADC Conversions:** 4-sample continuous averaging finishes in ~160 ms, ensuring fresh conversion data on every 200 ms cycle.
> 2. **Core 1 `control_task`:** Executes closed-loop PID and FSM calculations deterministically.
> 3. **QuickPID Timing:** Scaled to sample time $\Delta t = 0.2\text{ s}$.
> 4. **Thermal Simulation:** Advances by exact $\Delta t = 0.2\text{ s}$ increments in mock mode.

### 2.4 `config::Schema` & `config::Buzzer`
- **Schema Versions:** `MACHINE_SETTINGS = 1`, `REFLOW_PROFILE = 1`, `PID_LIBRARY = 1`. Enables forward-compatible schema migrations in LittleFS JSON files.
- **Acoustic Feedback:** `PREHEAT_DONE_BEEP_MS = 1000` (1 short beep), `REFLOW_DONE_BEEP_MS = 3000` (3-second continuous tone upon soldering cycle completion).

---

## 3. Hardware GPIO Pin Mapping (`config::PinConfig`)

Hardware allocation is centralized in `struct PinConfig` to eliminate conflicting pin assignments on the ESP32-S3:

```cpp
#include "config/pin_config.hpp"
```

| Functional Subsystem | Identifier | GPIO Number | Direction | Description |
| :--- | :--- | :---: | :---: | :--- |
| **SPI Sensor Bus** | `PinConfig::SPI_SCK` | `GPIO_NUM_12` | Output | SPI Master Clock (MAX31856) |
| | `PinConfig::SPI_MISO` | `GPIO_NUM_13` | Input | SPI Master-In-Slave-Out |
| | `PinConfig::SPI_MOSI` | `GPIO_NUM_11` | Output | SPI Master-Out-Slave-In |
| | `PinConfig::CS_TOP` | `GPIO_NUM_10` | Output | Active-LOW Chip Select (Top Thermocouple) |
| | `PinConfig::CS_BOTTOM`| `GPIO_NUM_9` | Output | Active-LOW Chip Select (Bottom Thermocouple) |
| **SSR Heaters** | `PinConfig::SSR_TOP` | `GPIO_NUM_4` | Output | Top Ceramic Heater SSR Control |
| | `PinConfig::SSR_BOTTOM`| `GPIO_NUM_5` | Output | Bottom IR Pre-Heater SSR Control |
| **Auxiliary Actuators**| `PinConfig::FAN` | `GPIO_NUM_6` | Output | 24V Forced-Air Cooling Fan Driver |
| | `PinConfig::LAMP` | `GPIO_NUM_7` | Output | 24V PCB Inspection Lamp Driver |
| | `PinConfig::BUZZER` | `GPIO_NUM_15`| Output | Acoustic Alert Transducer Driver |
| **Front-Panel Buttons**| `PinConfig::BTN_START`| `GPIO_NUM_1` | Input | Start Push-Button (Active-LOW, Pulled-Up) |
| | `PinConfig::BTN_STOP` | `GPIO_NUM_2` | Input | Stop Push-Button (Active-LOW, Pulled-Up) |
| **Override Switches** | `PinConfig::SW_FAN` | `GPIO_NUM_3` | Input | Fan Override Switch (Latching Contact) |
| | `PinConfig::SW_LAMP` | `GPIO_NUM_8` | Input | Lamp Override Switch (Latching Contact) |

---

## 4. Domain Models & Validation Engine

Each domain model provides a self-validating `.validate()` method returning `config::ValidationResult`:

```cpp
struct ValidationResult {
    bool valid = true;                  ///< True if all parameters satisfy central limits
    const char* errorMessage = nullptr; ///< Error explanation on failure, or nullptr
};
```

### 4.1 `ProfileStep` & `ReflowProfile`
Represents an entire dual-channel reflow profile with independent heating profiles for the Top heater and Bottom pre-heater:

```cpp
struct ProfileStep {
    float    temp = 100.0f; // Target temperature (30..300°C)
    uint32_t time = 10;     // Hold duration (0..600s)
    float    ramp = 1.0f;   // Ramp rate (0.1..10.0°C/s)
    
    ValidationResult validate() const;
};

struct ReflowProfile {
    std::string name;                     // Profile display name (1..30 chars)
    std::string file;                     // Storage filename (max 40 chars, no traversal)
    std::vector<ProfileStep> stepsTop;    // Up to 10 steps for Top heater
    std::vector<ProfileStep> stepsBottom; // Up to 10 steps for Bottom heater
    
    ValidationResult validate() const;
};
```

**Security & Integrity Checks in `ReflowProfile::validate()`:**
1. Rejects empty names and names exceeding `Limits::MAX_NAME_LENGTH` (30 chars).
2. Prevents directory traversal attacks (`..`, `/`, `\`) in profile filenames.
3. Enforces at least one step in either channel.
4. Restricts steps to `Limits::MAX_PROFILE_STEPS` (10 per channel).
5. Recursively executes `ProfileStep::validate()` on each individual step.

### 4.2 `PidPoint` & `PidLibrary`
Defines temperature-dependent gain scheduling points for piecewise linear gain interpolation:

```cpp
struct PidPoint {
    float temp = 100.0f; // Setpoint (°C)
    float kp   = 2.0f;   // Proportional gain (0..100)
    float ki   = 0.05f;  // Integral gain (0..10)
    float kd   = 1.0f;   // Derivative gain (0..100)
    
    ValidationResult validate() const;
};

struct PidLibrary {
    std::vector<PidPoint> top;    // Up to 15 points for Top heater
    std::vector<PidPoint> bottom; // Up to 15 points for Bottom heater
    
    ValidationResult validate() const;
};
```

### 4.3 `MachineSettings`
Master runtime configuration structure containing:
- **System & UI Preferences:** Simulation mode toggle, active language (`"de"`, `"en"`), theme, buzzer enabled flag, default boot profile name, HUD display toggles.
- **Thermal Safety Watchdog Limits:** Maximum/minimum allowable temperatures, cooling safe-to-touch threshold, stuck SSR rise threshold/window, heater no-rise threshold/timeout.
- **FSM Hold Gate & Settle Timings:** Lower/upper hold tolerances (0.5..30.0 °C), temperature stability settle duration (1..60 s).
- **Process Timings:** Fan cooling start delay (0..300 s), fan cooling runtime (10..600 s).
- **Sensor Parameters:** EMA filter enable/alpha (0.01..1.0), consecutive fault streak limit (1..20), cold-junction calibration offsets (-8.0..+7.9 °C).
- **SSR Burst-Fire Windows:** Top and Bottom time-proportional PWM window durations (500..4000 ms).
- **PID Default Gains:** Baseline fallback gains for Top and Bottom heaters.

---

## 5. Usage Example

```cpp
#include "config/machine_config.hpp"
#include "config/pin_config.hpp"
#include "esp_log.h"

static const char* TAG = "CONFIG_EXAMPLE";

void configure_system()
{
    // 1. Instantiate default machine settings
    config::MachineSettings settings;
    settings.simulationMode = false;
    settings.maxTempTop = 270.0f;
    settings.topBurstWindowMs = 1000;

    // 2. Validate configuration
    config::ValidationResult res = settings.validate();
    if (!res.valid) {
        ESP_LOGE(TAG, "Configuration invalid: %s", res.errorMessage);
        return;
    }
    ESP_LOGI(TAG, "Settings valid, control loop running at %lu ms", 
             config::Timing::CONTROL_LOOP_PERIOD_MS);

    // 3. Construct a dual-channel Reflow Profile
    config::ReflowProfile profile;
    profile.name = "Lead-Free SAC305";
    profile.file = "sac305.json";

    config::ProfileStep soak;
    soak.temp = 150.0f;
    soak.time = 60;
    soak.ramp = 1.5f;

    config::ProfileStep reflow;
    reflow.temp = 235.0f;
    reflow.time = 30;
    reflow.ramp = 2.0f;

    profile.stepsTop.push_back(soak);
    profile.stepsTop.push_back(reflow);

    config::ValidationResult profRes = profile.validate();
    if (profRes.valid) {
        ESP_LOGI(TAG, "Profile '%s' validated successfully with %zu steps",
                 profile.name.c_str(), profile.stepsTop.size());
    }

    // 4. Access hardware GPIO constants
    gpio_num_t ssrPin = config::PinConfig::SSR_TOP;
    ESP_LOGI(TAG, "Top heater SSR bound to GPIO %d", ssrPin);
}
```

---

## 6. Unit Tests & Automated Verification

The component includes comprehensive automated Unity unit tests located in [`test/test_config.cpp`](test/test_config.cpp) verifying all architectural constraints and domain boundaries:

| Test Function | Verification Target |
| :--- | :--- |
| `test_config_pin_conflicts` | Verifies that all 14 hardware GPIO allocations in `PinConfig` are unique with zero overlapping pin numbers via `std::set`. |
| `test_config_default_values`| Asserts default fields in `MachineSettings` match documented architectural parameters. |
| `test_config_schema_versions`| Confirms all schema version numbers are strictly positive integers. |
| `test_config_settings_validation`| Rigorously tests boundary edges (min/max temps, burst windows, watchdogs, CJTO offsets, EMA alpha). |
| `test_config_profile_validation`| Validates profile names, rejects directory traversal (`../`), enforces step count limits ($\le 10$). |
| `test_config_pid_validation` | Checks PID library bounds, gain limits, and rejects invalid temperatures. |
| `test_config_buzzer_constants` | Verifies acoustic timing definitions (`1000 ms`, `3000 ms`). |
| `test_config_resolution_constants`| Ensures all input step resolution definitions are non-zero positive numbers. |

---

## 7. CMake Integration

The component is configured as a header-only library with minimal ESP-IDF dependencies:

```cmake
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio
)
```

To include `config` in other components, add `config` to their `REQUIRES` or `PRIV_REQUIRES` list:

```cmake
idf_component_register(
    SRCS "my_module.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config
)
```

---

## 8. 📜 License

Copyright (C) 2026 EM-OpenTech

This component is part of the BGA Reflow Controller project and is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)**.

* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `AGPL-3.0-or-later`

See the project root [`LICENSE`](../../LICENSE) file for the full license text.
