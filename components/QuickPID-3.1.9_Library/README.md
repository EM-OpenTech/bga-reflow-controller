# QuickPID Component for ESP-IDF (C++20)

A high-performance, feature-complete C++ PID controller component natively ported to **Espressif ESP-IDF v6.0.2**, based on the original **QuickPID Library (v3.1.9)** by David Lloyd.

---

## 1. Overview & Key Advantages

`QuickPID` is an enhanced industrial PID algorithm designed specifically for dynamic processes with significant thermal inertia, such as ceramic and quartz infrared heaters in BGA rework stations.

### Core Architecture & ESP-IDF Port Characteristics
* **64-Bit Microsecond Hardware Timer**: Replaces Arduino's 32-bit `micros()` with `esp_timer_get_time()`, completely eliminating integer rollover bugs after 71 minutes of runtime.
* **Modern C++20 Standards**: Zero Arduino framework dependencies; uses standard C++ headers (`<cstdint>`, `<algorithm>`, `<cmath>`), `std::clamp`, and strict `const`-correctness.
* **Deterministic FreeRTOS Task Execution**:
  * Supports **`Control::timer` mode**: PID computation executes unconditionally whenever called by a periodic FreeRTOS task (e.g. `vTaskDelayUntil`), delegating timing strictly to FreeRTOS.
  * Supports **`Control::automatic` mode**: Uses `esp_timer_get_time()` to enforce `sampleTimeUs` internally if called from variable loops.
* **Single-Writer & Thread-Safe Design**: Operates purely on local state; safe for multi-core assignment when encapsulated in task-isolated contexts (such as `control_task` on Core 1).
* **Bump-Less Transfer**: Smooth switching between manual, automatic, and tuning modes without output jolts via `Initialize()`.

---

## 2. Mathematical Foundation & Controller Modes

### A. Proportional Modes (`QuickPID::pMode`)

The proportional term can be calculated based on error, measurement, or a hybrid combination:

| Mode | Value | Mathematical Formula | Characteristics & Application |
| :--- | :---: | :--- | :--- |
| `pOnError` | `0` | $P = K_p \times (SP - PV)$ | **Proportional on Error** (Standard PID). Fast setpoint tracking, but generates large power spikes on sudden setpoint steps. |
| `pOnMeas` | `1` | $P = -K_p \times \Delta PV$ | **Proportional on Measurement**. Eliminates step kick on setpoint changes; ideal for multi-stage BGA reflow profiles where setpoints jump between profile zones. |
| `pOnErrorMeas` | `2` | $P = 0.5 \times P_{\text{error}} + 0.5 \times P_{\text{meas}}$ | **Hybrid (50/50)**. Balanced trade-off between ramp agility and overshoot suppression. |

---

### B. Derivative Modes (`QuickPID::dMode`)

| Mode | Value | Mathematical Formula | Characteristics & Application |
| :--- | :---: | :--- | :--- |
| `dOnMeas` | `1` | $D = -K_d \times \frac{\Delta PV}{\Delta t}$ | **Derivative on Measurement** (Default). Eliminates *derivative kick* when the setpoint steps abruptly. Strongly recommended for thermal loops. |
| `dOnError` | `0` | $D = K_d \times \frac{\Delta (SP - PV)}{\Delta t}$ | **Derivative on Error**. Responds immediately to setpoint slope changes, but introduces severe derivative spikes on step inputs. |

---

### C. Integral Anti-Windup Modes (`QuickPID::iAwMode`)

Prevents saturation of the integral accumulator during actuator limits (0% to 100% duty cycle):

| Mode | Value | Mechanism |
| :--- | :---: | :--- |
| `iAwCondition` | `0` | **Conditional Anti-Windup** (Default). Evaluates whether the projected PI output $(P + I)$ exceeds output limits while the error is still expanding in the saturating direction. If saturated, accumulation halts, preventing deep integrator windup and severe overshoot upon reaching setpoint. |
| `iAwClamp` | `1` | **Hard Clamping**. Clamps the accumulated integral sum (`outputSum`) strictly between `outMin` and `outMax`. |
| `iAwOff` | `2` | **Disabled**. Raw integration without saturation protection (not recommended for heating elements). |

---

### D. Controller Action (`QuickPID::Action`)

* `Action::direct` (`0`): Output increases when process variable is below setpoint ($PV < SP$). Used for **heaters** (increasing power increases temperature).
* `Action::reverse` (`1`): Output increases when process variable is above setpoint ($PV > SP$). Used for **cooling fans** and refrigeration.

---

### E. Operating Modes (`QuickPID::Control`)

* `Control::manual` (`0`): PID calculation disabled; output variable is controlled manually.
* `Control::automatic` (`1`): Periodic computation governed by `sampleTimeUs` via `esp_timer_get_time()`.
* `Control::timer` (`2`): Unconditional computation on every `Compute()` call; designed for FreeRTOS tasks where the period is managed externally by `vTaskDelayUntil()`.
* `Control::toggle` (`3`): Toggles between manual and automatic mode.

---

## 3. Integration & Usage Example

```cpp
#include "QuickPID.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Process variables
float currentTemperature = 25.0f;
float heaterPowerDuty    = 0.0f;    // 0.0% to 100.0%
float targetSetpoint     = 180.0f;  // Preheat plateau 180°C

// Instantiate QuickPID with BGA reflow defaults
QuickPID topHeaterPid(
    &currentTemperature,
    &heaterPowerDuty,
    &targetSetpoint,
    2.5f, 0.05f, 15.0f,               // Kp, Ki, Kd
    QuickPID::pMode::pOnMeas,         // Proportional on measurement (no step kick)
    QuickPID::dMode::dOnMeas,         // Derivative on measurement
    QuickPID::iAwMode::iAwCondition,  // Dynamic conditional anti-windup
    QuickPID::Action::direct          // Direct action (heating)
);

void control_loop_task(void* pvParameters) {
    topHeaterPid.SetOutputLimits(0.0f, 100.0f);     // Clamp output to 0..100%
    topHeaterPid.SetSampleTimeUs(200000);           // 200 ms (5 Hz)
    topHeaterPid.SetMode(QuickPID::Control::timer); // Driven by deterministic FreeRTOS delay
    topHeaterPid.Initialize();

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(200);   // 200 ms control loop

    while (true) {
        // 1. Read temperature from sensor
        currentTemperature = readThermocouple();

        // 2. Compute PID output (runs unconditionally in timer mode)
        if (topHeaterPid.Compute()) {
            // 3. Apply calculated duty cycle to actuator / burst-fire manager
            applyHeaterPwm(heaterPowerDuty);
        }

        vTaskDelayUntil(&lastWakeTime, period);
    }
}
```

---

## 4. API Reference

### Constructors
```cpp
QuickPID();
QuickPID(float *Input, float *Output, float *Setpoint);
QuickPID(float *Input, float *Output, float *Setpoint,
         float Kp, float Ki, float Kd, Action action);
QuickPID(float *Input, float *Output, float *Setpoint,
         float Kp, float Ki, float Kd,
         pMode pMode, dMode dMode, iAwMode iAwMode, Action action);
```

### Control & Tuning
* `bool Compute()`: Runs one PID calculation cycle. Returns `true` if new output was calculated, `false` otherwise.
* `void SetMode(Control mode)` / `SetMode(uint8_t mode)`: Configures operating mode.
* `void SetOutputLimits(float min, float max)`: Defines output saturation limits (default: `0.0f` to `100.0f`).
* `void SetTunings(float kp, float ki, float kd)`: Updates gains on-the-fly while retaining current computational modes.
* `void SetTunings(float kp, float ki, float kd, pMode p, dMode d, iAwMode aw)`: Updates gains and changes modes simultaneously.
* `void SetSampleTimeUs(uint32_t us)`: Sets sample time in microseconds (used in `automatic` mode).
* `void SetControllerDirection(Action action)`: Configures direct vs. reverse acting direction.
* `void SetProportionalMode(pMode mode)`: Configures proportional calculation mode (`pOnError`, `pOnMeas`, `pOnErrorMeas`).
* `void SetDerivativeMode(dMode mode)`: Configures derivative calculation mode (`dOnError`, `dOnMeas`).
* `void SetAntiWindupMode(iAwMode mode)`: Configures anti-windup algorithm (`iAwCondition`, `iAwClamp`, `iAwOff`).
* `void SetOutputSum(float sum)`: Directly preloads the integral accumulator.
* `void Initialize()`: Executes bumpless transfer into automatic mode.
* `void Reset()`: Clears all terms (`pTerm`, `iTerm`, `dTerm`), accumulator (`outputSum`), and timing state.

### Telemetry & Query Functions
* `float GetKp() const`: Configured proportional gain.
* `float GetKi() const`: Configured integral gain.
* `float GetKd() const`: Configured derivative gain.
* `float GetPterm() const`: Current proportional term contribution.
* `float GetIterm() const`: Current integral term contribution.
* `float GetDterm() const`: Current derivative term contribution.
* `float GetOutputSum() const`: Current integral accumulator value.
* `uint8_t GetMode() const`: Current operating mode (`0`=manual, `1`=automatic, `2`=timer, `3`=toggle).
* `uint8_t GetDirection() const`: Current action direction (`0`=direct, `1`=reverse).
* `uint8_t GetPmode() const`: Proportional mode enum value.
* `uint8_t GetDmode() const`: Derivative mode enum value.
* `uint8_t GetAwMode() const`: Anti-windup mode enum value.

---

## 5. CMake Integration

Add to your component or project `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "your_source.cpp"
    INCLUDE_DIRS "include"
    REQUIRES "QuickPID-3.1.9_Library"
)
```

---

## 6. 📜 License & Attribution

Copyright (C) 2026 EM-OpenTech  
Copyright (C) 2023 David Lloyd (`dlloydev@testcor.ca`)

This component is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)** with attribution to the original author under the **MIT License**.

* **SPDX-FileCopyrightText:** `2023 David Lloyd <dlloydev@testcor.ca>`
* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `MIT AND AGPL-3.0-or-later`

* **Original QuickPID Library for Arduino:** Licensed under [MIT](LICENSE) (David Lloyd).
* **ESP-IDF v6.0.2 C++20 Port & Modifications:** Licensed under [AGPL-3.0-or-later](../../LICENSE) (EM-OpenTech).

For third-party notices, see [`THIRD-PARTY-NOTICES.md`](../../THIRD-PARTY-NOTICES.md).

