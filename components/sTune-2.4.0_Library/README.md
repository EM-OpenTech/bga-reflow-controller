# sTune & sTan PID Autotuner Component for ESP-IDF (C++20)

A high-performance, open-loop PID autotuner component natively ported to **Espressif ESP-IDF v6.0.2**, based on the original **sTune Library (v2.4.0)** and **sTan** sliding tangent circular buffer by David Lloyd (Dlloydev).

---

## 1. Overview & Principle of Operation

`sTune` is an open-loop PID autotuning engine based on the **S-curve Inflection Point (IP)** test method. By applying a single step output power and tracking the reaction curve up to its point of maximum slope (Wendepunkt / Inflection Point), it determines the First-Order Plus Dead Time (FOPDT) transfer function parameters in approximately **$\frac{1}{2}\tau$ (half the process time constant)**.

Compared to traditional closed-loop relay oscillation (Ziegler-Nichols) or full 5-Tau step tests, this method drastically reduces testing time, minimizes thermal stress, and avoids overshooting safety limits on high-inertia heating systems (such as ceramic/quartz BGA preheaters).

### FOPDT Process Model
The thermal process is modeled as:
$$G(s) = \frac{K_u \cdot e^{-t_d \cdot s}}{\tau \cdot s + 1}$$

Where:
* $K_u$: **Process Gain** ($\Delta \text{Output} \rightarrow \Delta \text{Temperature}$)
* $t_d$ ($L$): **Apparent Dead Time** (thermal transport and sensor delay before temperature begins rising)
* $\tau$ ($T$): **Process Time Constant** (time to reach ~63.2% of total temperature rise)

```
              Temperature (°C)
                    ^                       ___--- Apparent pvMax
                    |                  _--/
                    |               _ /
  Inflection Point  |------------->* (Maximum slope: ΔT/Δt)
         (Wendepunkt)|            _ /
                    |         _--/
      Start Temp    +-------/
                    |<-td-> |
                    +-------+-----------------------------> Time (s)
                       Dead  ½Tau
                       Time
```

---

## 2. Key Architecture & ESP-IDF Port Improvements

### A. Dual-Zone Multi-Instance Isolation
* In the original Arduino library, `tangent` was defined as a global static instance, preventing concurrent tuning of multiple heaters.
* In this port, `sTan tangent` is encapsulated as a **private class member** inside `sTune`. Top and Bottom heater zones can run independent autotuners concurrently without memory conflicts.

### B. Dynamic Memory & C++20 Vector Safety
* Replaced legacy raw dynamic heap pointers (`new float[]`) in `sTan` with standard `std::vector<float>`, eliminating memory leaks upon reconfiguration.

### C. 64-Bit Microsecond Hardware Timer
* Replaces Arduino 32-bit `micros()`/`millis()` with `esp_timer_get_time()`, eliminating 71-minute integer overflow issues.

### D. Native ESP-IDF Logging & Diagnostics
* Replaced Arduino `Serial.print` with structured ESP-IDF logging (`ESP_LOGI(TAG, ...)`), allowing clean output routing over UART or WebSocket log buffers.

### E. Integrated Emergency Thermal Cutoff
* Built-in `SetEmergencyStop(float eStopTemp)` aborts the step test immediately if the process variable crosses a safe threshold, protecting testbenches and PCBs.

---

## 3. Mathematical Inflection Point Detection (`sTan`)

`sTan` (**Sliding Tangent Line**) maintains a moving average and a sliding tangent buffer sized to **~6% of the configured total sample count**:

1. **Moving Average Filtering (`sTan::avgVal`)**: Suppresses raw thermocouple ADC noise.
2. **Tangent Slope Tracking (`sTan::slope`)**: Measures the gradient between the newest sample ("head") and oldest sample ("tail"):
   $$\text{pvTangent} = \text{pvAvg} - \text{tangent.startVal()}$$
3. **Inflection Point Lock-in**: As heat diffuses through the ceramic plate, the rate of temperature rise accelerates to a maximum and subsequently decelerates. When the change in slope reverses sign, the inflection point ($IP$) is locked.
4. **Apparent Maximum Temperature Extrapolation**:
   $$PV_{\text{max}} = PV_{IP} + \text{slope}_{IP} \times k_{\text{exp}} \quad \text{with } k_{\text{exp}} = 4.3004 = \frac{e}{e - 1}$$

---

## 4. Supported Tuning Rules & Methods

`sTune` calculates optimal PID/PI gains using 10 standard industrial tuning algorithms:

| Tuning Method Enum | Recommended Process | Target Characteristics |
| :--- | :--- | :--- |
| `CohenCoon_PID` | **Ceramic & Quartz Reflow Heaters** | **Primary choice for BGA reflow**. Excellent performance on processes with notable dead time ($t_d$). Fast response approximating $\frac{1}{4}$ decay ratio. |
| `CohenCoon_PI` | Systems sensitive to derivative noise | Same as Cohen-Coon PID, but with derivative action disabled ($K_d = 0$). |
| `NoOvershoot_PID` | Fragile semiconductor chips | Chien-Hrones-Reswick (CHR) setpoint tracking rule tuned strictly for **0% overshoot**. Conservative ramp. |
| `NoOvershoot_PI` | Sensitive processes without D-term | Chien-Hrones-Reswick PI with zero overshoot. |
| `ZN_PID` | General processes | Classic Ziegler-Nichols open-loop reaction curve rule with $\frac{1}{4}$ amplitude decay ratio. High agility, moderate overshoot. |
| `ZN_PI` | Systems prone to derivative oscillation | Classic Ziegler-Nichols PI rule. |
| `DampedOsc_PID` | Marginally stable thermal plants | Damped oscillation method designed to suppress resonant ringing. |
| `DampedOsc_PI` | Resonant systems without D-term | Damped oscillation PI method. |
| `Mixed_PID` | Universal fallback | Composite arithmetic average of gains from `ZN_PID`, `DampedOsc_PID`, `NoOvershoot_PID`, and `CohenCoon_PID`. |
| `Mixed_PI` | Universal fallback without D-term | Composite arithmetic average of gains from `ZN_PI`, `DampedOsc_PI`, `NoOvershoot_PI`, and `CohenCoon_PI`. |

---

## 5. Autotuner State Machine & Lifecycle

The execution loop (`tuner.Run()`) transitions through the following states (`sTune::TunerStatus`):

```
+------------+       Settling Elapsed       +----------+       Inflection Point Reached       +-------------+
|   sample   | ---------------------------> |   test   | -----------------------------------> |   tunings   |
| (Baseline) |                              | (Step On)|                                      | (Completed) |
+------------+                              +----------+                                      +-------------+
      |                                           |                                                  |
      +-------------------------------------------+--------------------------------------------------+
                                        | (Emergency Stop Abort)
                                        v
                                 +--------------+
                                 | eStop Trigger|
                                 +--------------+
```

| State Code | Identifier | Description |
| :---: | :--- | :--- |
| `0` | `sTune::sample` | Baseline settling phase: records initial process variable and stabilizes readings over `settleTimeSec`. |
| `1` | `sTune::test` | Step test active: applies `outputStep` to the actuator and tracks tangent slope on every sample. |
| `2` | `sTune::tunings` | Inflection point identified: calculations completed, parameters ($K_p, K_i, K_d$) ready for retrieval. |
| `3` | `sTune::runPid` | Autotuner finished; controller switched to PID execution. |
| `4` | `sTune::timerPid`| Timer-based PID execution phase. |

---

## 6. Usage Example (ESP-IDF FreeRTOS Task)

```cpp
#include "sTune.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AutotuneTask";

float currentTemp = 25.0f;
float testOutput  = 0.0f;

// Instantiate sTune: Cohen-Coon rule, Direct Inflection Point, Summary reporting
sTune tuner(&currentTemp, &testOutput, sTune::CohenCoon_PID, sTune::directIP, sTune::printSUMMARY);

void autotune_task(void *pvParameters) {
    // Parameter configuration for ceramic BGA plate:
    //   inputSpan:     300.0 °C (0..300°C measurement range)
    //   outputSpan:    100.0 %  (0..100% power)
    //   outputStart:     0.0 %  (initial power during settling)
    //   outputStep:     80.0 %  (step power applied during test)
    //   testTimeSec:     300 s  (estimated maximum test duration)
    //   settleTimeSec:    15 s  (initial baseline settling)
    //   samples:         300    (total samples across testTimeSec)
    tuner.Configure(300.0f, 100.0f, 0.0f, 80.0f, 300, 15, 300);
    tuner.SetEmergencyStop(240.0f); // Safety abort if temp exceeds 240°C

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000); // 1 Hz sample rate

    while (true) {
        currentTemp = readThermocouple();

        uint8_t status = tuner.Run();

        if (status == sTune::test) {
            // Apply step output to heating element
            setHeaterDuty(testOutput);
            ESP_LOGI(TAG, "Tuning Progress: %.1f%% | Temp: %.1f°C", tuner.GetProgress(), currentTemp);
        } else if (status == sTune::tunings) {
            // Autotune completed successfully!
            setHeaterDuty(0.0f); // Turn off heater immediately

            float kp = tuner.GetKp();
            float ki = tuner.GetKi();
            float kd = tuner.GetKd();

            ESP_LOGI(TAG, "Tuning COMPLETE!");
            ESP_LOGI(TAG, "  Ku: %.3f, Tu: %.1fs, DeadTime: %.1fs",
                     tuner.GetProcessGain(), tuner.GetTau(), tuner.GetDeadTime());
            ESP_LOGI(TAG, "  Calculated Gains -> Kp=%.3f, Ki=%.4f, Kd=%.2f", kp, ki, kd);
            break;
        }

        vTaskDelayUntil(&lastWakeTime, period);
    }

    vTaskDelete(NULL);
}
```

---

## 7. Complete API Reference

### Constructor & Configuration
* `sTune()`: Default constructor.
* `sTune(float *input, float *output, TuningMethod method, Action action, SerialMode mode)`: Parameterized constructor.
* `void Configure(float inSpan, float outSpan, float outStart, float outStep, uint32_t testTimeSec, uint32_t settleSec, uint16_t samples)`: Initializes step test parameters and dimensions circular buffers.
* `void SetEmergencyStop(float eStopTemp)`: Sets thermal emergency shutoff limit in &deg;C.
* `void SetControllerAction(Action action)`: Sets action direction and test mode (`directIP`, `direct5T`, `reverseIP`, `reverse5T`).
* `void SetTuningMethod(TuningMethod method)`: Selects calculation rule (e.g. `CohenCoon_PID`, `NoOvershoot_PID`).
* `void SetSerialMode(SerialMode mode)`: Sets log output level (`printOFF`, `printALL`, `printSUMMARY`, `printDEBUG`).

### Execution & Control
* `uint8_t Run()`: Periodic execution step. Returns current `TunerStatus` code.
* `void Reset()`: Flushes buffers and resets state machine to baseline.
* `void printTestRun()`: Logs test configuration parameters.
* `void printResults()`: Logs identified process parameters ($K_u, \tau, t_d$).
* `void printTunings()`: Logs computed PID gains for selected tuning rule.

### Process & Telemetry Getters
* `float GetKp()` / `GetKi()` / `GetKd()`: Calculated PID gains.
* `float GetTi() const` / `GetTd() const`: Integral and derivative time constants (seconds).
* `float GetProcessGain() const`: Identified process gain $K_u$.
* `float GetDeadTime() const`: Identified apparent dead time $t_d$ (seconds).
* `float GetTau() const`: Identified process time constant $\tau$ (seconds).
* `float GetProgress() const`: Test progress in percent (`0.0%` to `100.0%`).
* `uint16_t GetSampleCount() const`: Number of recorded samples.
* `uint16_t GetTotalSamples() const`: Target sample count.
* `void GetAutoTunings(float *kp, float *ki, float *kd)`: Copies calculated gains into destination pointers.

---

## 8. CMake Integration

Add to your component or project `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "your_source.cpp"
    INCLUDE_DIRS "include"
    REQUIRES "sTune-2.4.0_Library"
)
```

---

## 9. 📜 License & Attribution

Copyright (C) 2026 EM-OpenTech  
Copyright (C) 2022 David Lloyd (`dlloydev@testcor.ca`)

This component is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)** with attribution to the original author under the **MIT License**.

* **SPDX-FileCopyrightText:** `2022 David Lloyd <dlloydev@testcor.ca>`
* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `MIT AND AGPL-3.0-or-later`

* **Original sTune & sTan Library for Arduino:** Licensed under [MIT](LICENSE) (David Lloyd).
* **ESP-IDF v6.0.2 C++20 Port & Modifications:** Licensed under [AGPL-3.0-or-later](../../LICENSE) (EM-OpenTech).

For third-party notices, see [`THIRD-PARTY-NOTICES.md`](../../THIRD-PARTY-NOTICES.md).

