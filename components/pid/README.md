# PID & Autotuner Component (C++20)

High-level closed-loop temperature control and open-loop thermal autotuning component for the **BGA Reflow Controller**, running on **ESP-IDF v6.0.2** and **FreeRTOS**.

This component acts as the application-level bridge, wrapping and orchestrating the underlying calculation libraries:
* **`QuickPID-3.1.9_Library`**: The core closed-loop PID regulation engine.
* **`sTune-2.4.0_Library`**: The open-loop S-curve inflection point autotuning engine.

---

## 1. Architecture & Component Role

In an industrial dual-heater BGA rework station (Top infrared heater and Bottom preheater plate), temperature control poses severe challenges due to **large thermal inertia**, **transport delays (dead time)**, and **open-air convection**.

The `pid` component abstracts these physical realities into two high-level C++ classes:

```
                      +----------------------------------------+
                      |       control_task (Core 1, 5 Hz)      |
                      +----------------------------------------+
                                  |                 |
                    (Closed-Loop) |                 | (Open-Loop Tuning)
                                  v                 v
            +--------------------------+   +--------------------------+
            |   pid::PIDController     |   |    pid::PidAutotuner     |
            +--------------------------+   +--------------------------+
            |  Wraps: QuickPID v3.1.9  |   |  Wraps: sTune v2.4.0     |
            |  - pOnMeas / dOnMeas     |   |  - Inflection Point (½τ) |
            |  - iAwClamp Anti-Windup  |   |  - Emergency Stop Cutoff |
            |  - Mode: Control::timer  |   |  - Dual Channel Profiles |
            +--------------------------+   +--------------------------+
                         |                              |
                         +------------+    +------------+
                                      |    | (Gains: Kp, Ki, Kd)
                                      v    v
                           +------------------------+
                           |  Physical SSR Outputs  |
                           |  (via BurstFire/Output)|
                           +------------------------+
```

---

## 2. `pid::PIDController` – Closed-Loop Regulation

[`PIDController`](include/pid/pid_controller.hpp) manages real-time temperature regulation for a single heating zone (Top or Bottom).

### A. Tailored for Infrared Ceramic & Quartz Heaters
Standard textbook PID controllers often perform poorly on infrared heating elements, leading to burnt PCBs or broken solder joints. `PIDController` applies three specialized algorithms:

1. **Proportional on Measurement (`pOnMeas`)**:
   * Standard PID ($P$ on Error) injects a violent power surge whenever the target setpoint steps up (e.g., from 150 °C preheat to 215 °C reflow peak).
   * `pOnMeas` calculates $P = -K_p \times \Delta PV$. It reacts smoothly to the actual physical heating rate, completely eliminating setpoint step spikes and overshoot.
2. **Derivative on Measurement (`dOnMeas`)**:
   * Evaluates rate-of-change strictly based on sensor feedback ($D = -K_d \times \Delta PV / \Delta t$).
   * Prevents derivative kicks and jitter when transitioning between reflow profile phases.
3. **Integral Anti-Windup Clamping (`iAwClamp`)**:
   * Ceramic heaters exhibit long thermal lags (10–20 seconds). During this dead time, a classic integrator continues accumulating error, causing deep saturation.
   * `iAwClamp` strictly constrains the integral sum within `0.0%` to `100.0%`, ensuring immediate recovery once the target temperature is approached.

### B. Single Source of Truth Timing (5 Hz / 200 ms)
* Governed centrally by `config::Timing::CONTROL_LOOP_PERIOD_MS = 200` (`FIXED_PID_SAMPLE_TIME_MS`).
* In ESP-IDF, deterministic timing is driven by FreeRTOS `control_task` on Core 1 using `vTaskDelayUntil`.
* **The `Control::timer` Pattern**: Calling `setAutomatic(true)` under the hood sets QuickPID's mode to `QuickPID::Control::timer`. This bypasses internal microsecond time checks and guarantees that every call to `compute()` inside the 200 ms loop executes unconditionally without RTOS scheduling jitter.
* Internal gain scaling matches the 200 ms period:
  $$K_{i,\text{internal}} = K_i \times 0.2\,\text{s}, \quad K_{d,\text{internal}} = \frac{K_d}{0.2\,\text{s}}$$

### C. Bumpless Manual Transfer
* Switching from Manual mode (user-defined power override) to Automatic PID mode synchronizes the internal integral accumulator (`outputSum = _output`) to prevent sudden relay bursts.

---

## 3. `pid::PidAutotuner` – Open-Loop Characterization

[`PidAutotuner`](include/pid/pid_autotuner.hpp) implements the **S-curve Inflection Point (IP)** test method based on `sTune`.

### A. Principle of Operation
Instead of oscillating the heater around a setpoint for 30–60 minutes, `PidAutotuner`:
1. Settles the heater at baseline for 15 seconds.
2. Applies a defined open-loop power step (30 % for Top, 40 % for Bottom).
3. Tracks the temperature rise curve until the **Inflection Point (point of maximum slope $\Delta T / \Delta t$)** is reached.
4. Extrapolates the First-Order Plus Dead Time (FOPDT) system parameters ($K_u$, $\tau$, $t_d$) and computes optimal gains using the **No-Overshoot (CHR)** and **Cohen-Coon** tuning rules.
5. The test completes in approximately **$\frac{1}{2}\tau$**, saving time and preventing component thermal stress.

### B. Pre-Configured Dual Channel Profiles

| Parameter | Top Heater (`_isTop = true`) | Bottom Heater (`_isTop = false`) |
| :--- | :---: | :---: |
| **Heater Type** | Fast quartz / ceramic dark-emitter | Large thermal mass ceramic plate |
| **Step Power** | 30.0 % | 40.0 % |
| **Max Test Time** | 250 seconds | 400 seconds |
| **Settling Time** | 15 seconds | 15 seconds |
| **Total Samples** | 250 (1 sample/sec) | 400 (1 sample/sec) |
| **Emergency Cutoff** | $\min(T_{\text{target}} + 40^\circ\text{C},\, 300^\circ\text{C})$ | $\min(T_{\text{target}} + 40^\circ\text{C},\, 300^\circ\text{C})$ |

### C. Multi-Layer Hardware Safety
* **Emergency Threshold (`_eStopTemp`)**: Dynamically set to $40^\circ\text{C}$ above the target setpoint, capped at `config::Limits::MAX_TEMPERATURE` (300 °C).
* If the sensor reading exceeds `_eStopTemp` at any point during the step test, `step()` immediately calls `abort()`, cuts output power to 0.0 %, and logs an error.

---

## 4. Usage & Integration Example

### A. Standard Closed-Loop Operation (in `control_task`)

```cpp
#include "pid/pid_controller.hpp"

// Instantiate with machine default gains
pid::PIDController topPid(2.5f, 0.05f, 15.0f);

void app_init() {
    topPid.begin();
    topPid.setOutputLimits(0.0f, 100.0f);
    topPid.setSetpoint(180.0f);
    topPid.setAutomatic(true); // Internally engages Control::timer mode
}

void control_loop_200ms() {
    // 1. Feed current temperature
    topPid.setInput(readTopSensor());

    // 2. Compute output
    if (topPid.compute()) {
        float powerPercent = topPid.getOutput(); // 0.0% to 100.0%
        applyBurstFireDuty(powerPercent);
    }
}
```

### B. Running an Autotuning Cycle

```cpp
#include "pid/pid_autotuner.hpp"

pid::PidAutotuner autotuner;

void start_tuning() {
    float startTemp = readSensor();
    float targetTemp = 150.0f;
    autotuner.begin(true /* isTop */, targetTemp, startTemp);
}

void tuning_loop_200ms() {
    if (autotuner.isRunning()) {
        float outputPower = 0.0f;
        float currentTemp = readSensor();

        bool stillRunning = autotuner.step(currentTemp, outputPower);
        applyBurstFireDuty(outputPower);

        if (!stillRunning && autotuner.isFinished()) {
            // Apply new gains directly to active controller
            autotuner.applyResults(topPid);

            float kp, ki, kd;
            autotuner.getResults(kp, ki, kd);
            // Save kp, ki, kd to LittleFS settings...
        }
    }
}
```

---

## 5. API Reference

### `pid::PIDController`

| Method | Description |
| :--- | :--- |
| `PIDController(float kp, float ki, float kd)` | Constructor with default or specified initial gains. |
| `void begin()` | Initializes output limits (`0..100%`), sample time (`200 ms`), and enters manual mode. |
| `void setTunings(float kp, float ki, float kd)` | Dynamic gain update (e.g. Gain Scheduling or after autotune). |
| `void setSetpoint(float setpoint)` | Sets target temperature in °C. |
| `void setInput(float input)` | Updates latest measured/filtered process temperature in °C. |
| `bool compute()` | Computes PID output. Returns `true` if calculated, `false` if manual. |
| `void reset()` | Resets integral sum and history state (bumpless). |
| `void setAutomatic(bool enabled)` | Toggles between Auto PID (`Control::timer`) and Manual mode. |
| `void setManualOutput(float percent)` | Sets manual output power (clamped to min/max). |
| `void setOutputLimits(float min, float max)` | Configures output saturation clamping boundaries. |
| `float getOutput() const` | Returns current computed power percentage (0.0% to 100.0%). |
| `float getPterm() / getIterm() / getDterm()` | Detailed term diagnostics for telemetry inspection. |

### `pid::PidAutotuner`

| Method | Description |
| :--- | :--- |
| `PidAutotuner()` | Constructor initializing underlying `sTune` engine. |
| `void begin(bool isTop, float targetTemp, float startTemp)` | Starts step test with channel-specific profile and sets safety limits. |
| `bool step(float currentTemp, float &outputPower)` | Cyclic evaluation step. Returns `true` while running, `false` when done/aborted. |
| `void abort()` | Aborts tuning immediately, resets state, and sets power to 0.0 %. |
| `void applyResults(PIDController &pid)` | Transfers calculated $K_p, K_i, K_d$ directly to a `PIDController`. |
| `bool isRunning() const` | Returns `true` if test is currently in progress. |
| `bool isFinished() const` | Returns `true` if test completed successfully with valid gains. |
| `float getProgressPercent() const` | Returns tuning progress from 0.0 % to 100.0 %. |
| `void getResults(float &kp, float &ki, float &kd) const` | Copies identified gains into destination variables. |

---

## 6. Unit Tests & Verification

The component includes 100 % automated unit test coverage located in [`test/`](test/):

1. **[`test_pid.cpp`](test/test_pid.cpp)**:
   * Proportional, integral, and derivative response verification.
   * Clamping limits (0 % to 100 %).
   * Manual override and bumpless transfer synchronization.
   * Gain scheduling updates.
   * Term diagnostics breakdown (`getPterm`, `getIterm`, `getDterm`).
   * Floating-point sanitization against `NaN` and `Inf` inputs (`test_pid_nan_safety`).
2. **[`test_autotuner.cpp`](test/test_autotuner.cpp)**:
   * Initial state and reset validation.
   * Channel parameter switching (Top vs. Bottom test profiles).
   * Emergency cutoff trigger on temperature overshoot.
   * Boundary clamping against `config::Limits::MAX_TEMPERATURE`.
   * Result transfer to `PIDController`.
   * Rejection of `NaN` temperature inputs (`test_autotuner_nan_input_protection`).
   * Boundary validation of autotuned gains before result application (`test_autotuner_unrealistic_gains_rejection`).

---

## 7. CMake Integration

Defined in [`CMakeLists.txt`](CMakeLists.txt):

```cmake
idf_component_register(
    SRCS
        "pid_controller.cpp"
        "pid_autotuner.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        "QuickPID-3.1.9_Library"
        "sTune-2.4.0_Library"
        "config"
    PRIV_REQUIRES
        "log"
)
```

---

## 8. 📜 License

Copyright (C) 2026 EM-OpenTech

This component is part of the BGA Reflow Controller project and is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)**.

* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `AGPL-3.0-or-later`

See the project root [`LICENSE`](../../LICENSE) file for the full license text.
