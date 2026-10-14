# Hardware & Thermal Safety Watchdog Component (C++20)

Deterministic, hard real-time safety watchdog engine for the **BGA Reflow Controller**, running natively on **ESP-IDF v6.0.2** and **FreeRTOS**.

This component manages physical thermodynamic hazard monitoring, component failure detection (e.g., frozen solid-state relays, blown heating elements), floating-point arithmetic sanitization, and MAX31856 Layer-2 hardware fault flags. It acts as an autonomous hardware interlock system for dual-channel quartz and ceramic heater rework stations.

---

## 1. Overview & Architecture

Operating dual-channel heating elements (Top Infrared Heater and Bottom Preheater) reaching temperatures up to 280 °C requires an independent, hard real-time safety monitor. Thermal runaway, shorted SSRs, or disconnected thermocouple probes can destroy PCBs or cause thermal fires within seconds.

The `safety` component runs with the highest priority in the system and directly commands the `output::OutputManager` hardware interlock:

```
               +-----------------------------------------------------------+
               |                  ESP32-S3 Firmware Core 1                  |
               +-----------------------------------------------------------+
                                             |
             +-------------------------------+-------------------------------+
             |                                                               |
             v                                                               v
   [ sensor::MAX31856 ]                                            [ pid::QuickPID ]
    (Top & Bottom TC)                                               (Top & Bottom SSR)
             |                                                               |
   Latest Readings                                                 Active Power (0..100%)
   (Temp, Valid, Flags)                                            Target Setpoint (°C)
             |                                                               |
             +-------------------------------+-------------------------------+
                                             |
                                             v
                      +---------------------------------------------+
                      |        safety::SafetyWatchdog               |
                      |  - Execution: Core 1 @ 20 Hz (50 ms loop)   |
                      |  - Priority 10 (Highest in System)          |
                      |---------------------------------------------|
                      |  1. Floating-Point Hygiene (NaN / Inf)      |
                      |  2. Wire Break & SPI Validity Check         |
                      |  3. Layer-2 MAX31856 0x0F Register Decode   |
                      |  4. Absolute Overtemperature (e.g. >280°C)  |
                      |  5. Absolute Undertemperature (<5°C)        |
                      |  6. Stuck SSR Inversion Detection           |
                      |  7. Dynamic PCB Heater Failure (No-Rise)    |
                      +---------------------------------------------+
                                             |
                               [ Is Hazard Detected? ]
                                             |
                             +---------------+---------------+
                             | YES                           | NO
                             v                               v
            +---------------------------------+     [ Normal System Operation ]
            |  output::OutputManager          |     (SSR Burst-Fire PWM active)
            |  setInhibit(true)               |
            |---------------------------------|
            |  ALL SSR Gates Driven LOW       |
            |  Emergency Latched in FSM       |
            +---------------------------------+
```

### Core Execution Guarantees
* **Independent High-Priority Task:** Runs inside `safety_task` on Core 1 at **Priority 10** (every 50 ms / 20 Hz), running faster than the 5 Hz (200 ms) `control_task`.
* **Zero-Allocation Execution:** The watchdog maintains all state internally in flat structs (`ChannelWatchdogState`), executing deterministically without heap allocations or blocking synchronization primitives.
* **Thread-Safe Atomic Fault State:** The latched fault state (`_fault`) uses `std::atomic<SafetyFault>` with `std::memory_order_relaxed`, providing lock-free, race-free read access across Core 0 (Web REST/WS) and Core 1 (`safety_task` 20 Hz / `control_task`).
* **Direct Hardware Interlock:** When an anomaly is confirmed, `_outputManager.setInhibit(true)` is called immediately in the same cycle, forcing physical GPIOs to the safe de-energized level before the FSM is informed.

---

## 2. Multi-Tier Hazard Detection Matrix

| Level | Hazard Condition | Evaluation Trigger & Logic | Action & Latched Fault |
| :--- | :--- | :--- | :--- |
| **L0** | **Arithmetic Corruption** | `std::isnan(temp) \|\| std::isinf(temp)` | `SENSOR_FAULT_TOP` / `BOTTOM`<br>`setInhibit(true)` |
| **L1** | **Sensor Disconnect** | `!reading.isValid` (SPI timeout, CRC fault, bus stall) | `SENSOR_FAULT_TOP` / `BOTTOM`<br>`setInhibit(true)` |
| **L2** | **MAX31856 Hardware IC Fault** | `reading.fault.hasFault()` (Register `0x0F` non-zero) | `HARDWARE_IC_FAULT_TOP` / `BOTTOM`<br>`setInhibit(true)` |
| **L3** | **Absolute Overtemperature** | `temperature > maxAllowed` (Default: `280.0°C`) | `OVERTEMP_TOP` / `BOTTOM`<br>`setInhibit(true)` |
| **L4** | **Absolute Undertemperature** | `enableMinTempCheck && temperature < minAllowed` (`5.0°C`) | `UNDERTEMP_TOP` / `BOTTOM`<br>`setInhibit(true)` |
| **L5** | **Stuck / Shorted SSR** | Power requested $< 1.0\%$ while $T > 50.0^\circ\text{C}$, but $\Delta T > 5.0^\circ\text{C}$ across 10 s window | `STUCK_SSR_TOP` / `BOTTOM`<br>`setInhibit(true)` |
| **L6** | **Heater Failure (No-Rise)** | Power requested $> 99.0\%$ and not near target setpoint ($> 20.0^\circ\text{C}$ away), but $\Delta T < 3.0^\circ\text{C}$ across 15 s window | `NO_RISE_TOP` / `BOTTOM`<br>`setInhibit(true)` |

---

## 3. Advanced Diagnostic Mechanics

### 3.1 Stuck SSR Detection (Thermal Inversion)
Triac and MOSFET-based Solid-State Relays frequently fail shorted when subjected to high thermal cycling or voltage transients.
* **Condition:** When PID outputs demand zero power (`power < 1.0%`) on a hot station ($T > 50.0^\circ\text{C}$), temperature must strictly decay or plateau.
* **Mechanism:** The watchdog starts a sliding timer window (`stuckSsrWindowSec`, default 10 s). If actual temperature rises by more than `stuckSsrRiseThreshold` (default $5.0^\circ\text{C}$) during this 0%-power window, a stuck SSR is declared.

### 3.2 Dynamic PCB Heater Failure (No-Rise Hysteresis)
A blown heating element, disconnected mains cable, or uncoupled thermocouple could cause thermal runaway if PID integrates to 100% indefinitely.
* **Problem in Naive Watchdogs:** When a PCB reaches soak or peak temperature (e.g. $215^\circ\text{C}$ for Leaded or $245^\circ\text{C}$ for Lead-Free solder), temperature delta flattens naturally even if PID momentarily hits 100%, causing false trips.
* **Dynamic Setpoint Adaptation:** The watchdog checks whether the sensor is within $20.0^\circ\text{C}$ of the active target setpoint:
  $$\text{isNearTarget} = (\text{setpoint} > 0 \land (\text{setpoint} - T < 20^\circ\text{C})) \lor (T > T_\text{max} - 20^\circ\text{C})$$
  If `isNearTarget` is true, the No-Rise check is bypassed, completely preventing false trips near profile plateaus. If power drops below 80%, the tracking window resets with hysteresis.

---

## 4. Configuration Reference (`SafetyConfig`)

The watchdog parameters can be calibrated dynamically or loaded from flash storage (`/littlefs/config/settings.json`):

```cpp
namespace safety {

struct SafetyConfig {
    float    maxTempTop            = 280.0f;  ///< Maximum allowed temperature for top heater (°C)
    float    maxTempBottom         = 280.0f;  ///< Maximum allowed temperature for bottom heater (°C)
    float    minTempTop            = 5.0f;    ///< Minimum allowed temperature for top heater (°C)
    float    minTempBottom         = 5.0f;    ///< Minimum allowed temperature for bottom heater (°C)

    float    stuckSsrRiseThreshold = 5.0f;    ///< Rise threshold to trigger stuck SSR fault (°C)
    uint32_t stuckSsrWindowSec     = 10;      ///< Evaluation time window for stuck SSR detection (s)

    float    noRiseThreshold       = 3.0f;    ///< Minimum rise required when heater runs at 100% (°C)
    uint32_t noRiseTimeoutSec      = 15;      ///< Timeout before declaring heater failure / no-rise (s)

    bool     enableSafetyWatchdog  = true;    ///< Master enable for all safety watchdog checks
    bool     enableStuckSsrCheck   = true;    ///< Enable stuck SSR detection
    bool     enableNoRiseCheck     = true;    ///< Enable no-rise (heater failure) detection
    bool     enableMinTempCheck    = true;    ///< Enable undertemperature detection
};

} // namespace safety
```

---

## 5. C++20 Usage Example

```cpp
#include "safety/safety_watchdog.hpp"
#include "output/output_manager.hpp"
#include "sensor/max31856.hpp"

// 1. Instantiate hardware outputs and watchdog
output::OutputManager outputs;
outputs.init();

safety::SafetyConfig config;
config.maxTempTop    = 275.0f;
config.maxTempBottom = 265.0f;

safety::SafetyWatchdog watchdog(outputs, config);
watchdog.begin();

// 2. High-priority evaluation loop (e.g. 20 Hz FreeRTOS task)
void safety_task_step(const sensor::SensorReading& topReading,
                      const sensor::SensorReading& bottomReading,
                      float topDuty, float bottomDuty,
                      float topTarget, float bottomTarget)
{
    // Evaluate all physical and software limits
    bool hasHazard = watchdog.check(topReading, bottomReading,
                                    topDuty, bottomDuty,
                                    topTarget, bottomTarget);

    if (hasHazard) {
        // OutputManager is already inhibited by SafetyWatchdog!
        safety::SafetyFault fault = watchdog.getFault();
        const char* reason = watchdog.getFaultString();
        // Latch FSM into EMERGENCY_STOP state...
    }
}
```

---

## 6. Unit Testing & Verification (`test_safety.cpp`)

The component includes 13 Unity unit tests covering edge cases, arithmetic hygiene, timing windows, and fault latching:

| # | Test Function | Verified Behavior |
|---|:---|:---|
| 1 | `test_safety_clean` | Verifies clean initialization: zero faults, output inhibit cleared. |
| 2 | `test_safety_overtemp` | Verifies immediate trip and inhibit when Top channel exceeds $280^\circ\text{C}$. |
| 3 | `test_safety_bottom_overtemp` | Verifies immediate trip and inhibit when Bottom channel exceeds $280^\circ\text{C}$. |
| 4 | `test_safety_undertemp` | Verifies trip when $T < 5.0^\circ\text{C}$ and dynamic bypass via `enableMinTempCheck = false`. |
| 5 | `test_safety_disconnect` | Verifies trip when sensor wire breaks (`isValid = false`, `openCircuit = true`). |
| 6 | `test_safety_ic_hardware_fault` | Verifies Layer-2 hardware fault flag extraction from MAX31856 register `0x0F`. |
| 7 | `test_safety_stuck_ssr` | Verifies detection of heating while power demand is 0% ($T > 50^\circ\text{C}$). |
| 8 | `test_safety_no_rise` | Verifies detection of heater/SSR failure at 100% duty cycle away from setpoint. |
| 9 | `test_safety_reset` | Verifies clean reset cycle and safe release of hardware output inhibition. |
| 10 | `test_safety_fault_strings` | Verifies formatting and human-readable diagnostic messages. |
| 11 | `test_safety_bypass_disabled` | Verifies master bypass switch (`enableSafetyWatchdog = false`) for testbenches. |
| 12 | `test_safety_nan_protection` | Verifies floating-point arithmetic sanitization against `NaN` and `Inf`. |
| 13 | `test_safety_multi_channel_simultaneous_fault` | Verifies concurrent multi-channel emergency prioritization and latching. |

---

## 7. CMake Integration

Defined in [`CMakeLists.txt`](CMakeLists.txt):

```cmake
idf_component_register(
    SRCS "safety_watchdog.cpp"
    INCLUDE_DIRS "include"
    REQUIRES sensor output esp_timer
    PRIV_REQUIRES log
)
```

---

## 8. 📜 License

Copyright (C) 2026 EM-OpenTech

This component is part of the BGA Reflow Controller project and is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)**.

* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `AGPL-3.0-or-later`

See the project root [`LICENSE`](../../LICENSE) file for the full license text.

