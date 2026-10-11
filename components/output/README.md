# Output & Actuator Manager Component (C++20)

Hardware GPIO actuator driver and time-proportional Solid State Relay (SSR) burst-fire modulation component for the **BGA Reflow Controller**, running natively on **ESP-IDF v6.0.2** and **FreeRTOS**.

This component manages all physical electrical outputs (Top Heater SSR, Bottom Preheater SSR, Cooling Fan, Work Lamp, and Acoustic Buzzer) and implements hardware safety lockouts.

---

## 1. Overview & Architecture

Driving high-power 230V AC heating elements via zero-crossing Solid State Relays (SSRs) requires a specialized modulation strategy. High-frequency kHz PWM cannot be used with zero-crossing SSRs and AC mains power (50/60 Hz), as it would cause destructive harmonics, missed half-cycles, and intense electromagnetic interference (EMI).

The `output` component solves this through two coordinated C++ modules:

```
    +--------------------------------+       +---------------------------------+
    |   control_task (Core 1, 5 Hz)  |       |   safety_task (Core 1, 20 Hz)   |
    +--------------------------------+       +---------------------------------+
                   | (setPower 0..100%)                               |
                   v                                                  | (setInhibit true/false)
    +--------------------------------+                                |
    |      output::BurstFire         |                                |
    |  - Time-Proportional PWM       |                                |
    |  - Microsecond Drift-Free      |                                |
    +--------------------------------+                                |
                   |                                                  |
                   | (getState ON/OFF)                                |
                   v                                                  v
    +--------------------------------------------------------------------------+
    |                         output::OutputManager                            |
    |  - Hardware GPIO HAL (driver/gpio.h)                                     |
    |  - Active-HIGH vs. Active-LOW Polarity Inversion                         |
    |  - Hardware Pull-Up / Pull-Down Default Bias                             |
    |  - Dual Lockout: Atomic Safety Inhibit & Simulation Mode Inhibit        |
    +--------------------------------------------------------------------------+
          |               |                 |             |             |
          v               v                 v             v             v
     [ Top SSR ]   [ Bottom SSR ]       [ 24V Fan ]   [ 24V Lamp ]  [ Buzzer ]
       GPIO 4          GPIO 5              GPIO 6        GPIO 7       GPIO 15
```

---

## 2. Hardware Pinout & Default Channel Configuration

> [!NOTE]
> **Hardware Dependency (Example Configuration):**  
> The table below illustrates the default reference configuration. The actual polarity (`activeHigh`) and hardware bias resistors depend directly on the **driver circuitry and relay hardware utilized**:
> * **Relay Modules with Optocoupler Inputs:** Frequently operate as **Active-LOW** (a LOW logic level `0V` at the optocoupler cathode energizes the diode and activates the relay). An internal or external pull-up resistor is essential to hold the relay reliably de-energized during ESP32-S3 bootup.
> * **Directly Driven Solid State Relays (DC-SSR):** Typically operate as **Active-HIGH** (a HIGH logic level `3.3V` / `5V` energizes the SSR control input). A pull-down resistor is required to prevent floating gate states and spurious EMI triggering.
> * **MOSFET Driver Stages (e.g., 24V Fan / Lamp):** Discrete N-channel low-side MOSFETs typically switch with **Active-HIGH** logic.
>
> Polarity can be modified statically in `OutputSystemConfig` or dynamically at runtime using `setChannelPolarity(pin, activeHigh)` without reflashing.

Defined centrally in `config::PinConfig` (`components/config/include/config/pin_config.hpp`):

| Channel | GPIO Pin | Default Polarity (Reference) | Hardware Bias | Connected Hardware (Example) |
| :--- | :---: | :---: | :---: | :--- |
| **Top SSR** | `GPIO_NUM_4` | **Active-LOW** (`false`) | Pull-Up Enabled | Top infrared heater (e.g., optocoupled inverted relay module) |
| **Bottom SSR** | `GPIO_NUM_5` | **Active-HIGH** (`true`) | Pull-Down Enabled | Bottom ceramic preheater (e.g., direct DC-controlled SSR) |
| **Cooling Fan** | `GPIO_NUM_6` | **Active-HIGH** (`true`) | Pull-Down Enabled | 24V DC auxiliary cooling fan via N-channel MOSFET |
| **Work Lamp** | `GPIO_NUM_7` | **Active-HIGH** (`true`) | Pull-Down Enabled | 24V DC work inspection LED lamp via MOSFET |
| **Buzzer** | `GPIO_NUM_15` | **Active-HIGH** (`true`) | Pull-Down Enabled | Acoustic piezo alarm buzzer |

*All channels support dynamic polarity reconfiguration at runtime via `setChannelPolarity()` to accommodate different relay board revisions without firmware recompilation.*

---

## 3. `output::BurstFire` – Time-Proportional PWM

[`BurstFire`](include/output/burst_fire.hpp) converts a continuous PID power demand (`0.0%` to `100.0%`) into discrete ON/OFF duty cycles over a configurable time window (**Schwingungspaketsteuerung**).

### A. Mathematical Principle

Given a time window duration $T_{\text{window}}$ (e.g. 1000 ms) and a PID power request $P$ (e.g. 35.0 %):
$$t_{\text{ON}} = \frac{P}{100.0} \times T_{\text{window}} = 350\,\text{ms}$$
$$t_{\text{OFF}} = T_{\text{window}} - t_{\text{ON}} = 650\,\text{ms}$$

```
  Window Start                                                            Window End
  +-----------------------------------+-----------------------------------+
  |           SSR ON (350 ms)         |          SSR OFF (650 ms)         |
  +-----------------------------------+-----------------------------------+
  |<------------------------ 1000 ms Window ----------------------------->|
```

### B. Microsecond Precision & Drift Elimination
* **64-bit Hardware Timer (`esp_timer_get_time`)**: Tracks elapsed microseconds since boot without 71-minute rollover bugs.
* **Cumulative Drift Elimination**: Advances window timestamps mathematically (`_windowStartUs += windowsPassed * windowUs`) to prevent cumulative phase error across hours of operation.
* **Spurious Pulse Suppression**: `setWindowMs()` only synchronizes `_windowStartUs` when the window duration **actually changes**, preventing unintended relay blips during runtime settings reloads.
* **Validation Bounds**: Clamped strictly between `config::Limits::MIN_BURST_WINDOW_MS` (500 ms) and `MAX_BURST_WINDOW_MS` (4000 ms).

---

## 4. `output::OutputManager` – Actuator Hardware Layer

[`OutputManager`](include/output/output_manager.hpp) directly interfaces with the ESP32-S3 GPIO peripheral via `driver/gpio.h`.

### A. Boot-Safe Hardware Initialization (`begin()`)
* Safely configures all 5 pins using `gpio_config()`.
* All output pins are immediately driven to their **inactive (OFF) electrical state** during initialization to guarantee that heaters never glitch ON during MCU boot or brownout resets.

### B. Dual-Layer Hardware Safety Lockout

#### 1. Safety Inhibit Lockout (`_inhibit`)
* Driven directly by `safety_task` on Core 1 when a temperature fault, sensor disconnect, or runaway is detected.
* Uses lock-free `std::atomic<bool>` memory ordering.
* When active, calls to `setSsrTop(true)` and `setSsrBottom(true)` are completely suppressed at the driver level, forcing physical GPIOs strictly to OFF.

#### 2. Simulation Mode Hardware Lockout (`_simulationMode`)
* Enabled when the controller runs in **Simulation Mode** (dry-run profile testing and web interface demos without 230V hardware).
* Drives physical SSR pins strictly to OFF at the GPIO driver level.
* Allows the user to run full reflow profiles, verify PID graphs, and observe FSM transitions without any risk of bench heating.

---

## 5. FreeRTOS Task Integration

In the BGA Reflow Controller firmware, outputs are orchestrated across dedicated FreeRTOS tasks:

1. **`burstfire_task` (Core 1, 100 Hz / 10 ms)**:
   * Periodically updates `topBurst.update()` and `botBurst.update()`.
   * Immediately transmits calculated states to `outputs.setSsrTop()` and `outputs.setSsrBottom()`.
2. **`control_task` (Core 1, 5 Hz / 200 ms)**:
   * Periodically computes PID output power (0.0% - 100.0%) and passes it via `topBurst.setPower()` and `botBurst.setPower()`.
3. **`safety_task` (Core 1, 20 Hz / 50 ms)**:
   * Evaluates thermal envelopes and calls `outputs.setInhibit(true)` upon alarms.

---

## 6. Usage Example (ESP-IDF C++)

```cpp
#include "output/output_manager.hpp"
#include "output/burst_fire.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

output::OutputManager outputs;
output::BurstFire     topBurst(1000); // 1000 ms burst window

void app_main() {
    // 1. Initialize GPIO outputs safely
    outputs.begin();

    // 2. Set requested power (e.g., from PID controller: 40%)
    topBurst.setPower(40.0f);

    // 3. Periodic BurstFire task loop (100 Hz / 10 ms):
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10);

    while (true) {
        // Update burst-fire PWM calculation
        topBurst.update();

        // Write resulting ON/OFF state to physical GPIO
        outputs.setSsrTop(topBurst.getState());

        vTaskDelayUntil(&lastWakeTime, period);
    }
}
```

---

## 7. API Reference

### `output::BurstFire`

| Method | Description |
| :--- | :--- |
| `BurstFire(uint32_t windowMs)` | Constructor with initial window duration in ms (default: 1000 ms). |
| `void begin(uint32_t windowMs)` | Initializes starting timestamp and resets state to OFF. |
| `void setPower(float percent)` | Sets requested power percentage (clamped to `0.0%` .. `100.0%`). |
| `float getPower() const` | Returns current requested power percentage. |
| `void setWindowMs(uint32_t windowMs)` | Updates window duration (clamped to `500` .. `4000` ms). |
| `uint32_t getWindowMs() const` | Returns active window duration in ms. |
| `void update()` | Evaluates elapsed window time and recalculates SSR state. |
| `bool getState() const` | Returns active calculated state (`true` = ON, `false` = OFF). |
| `uint32_t getOnTimeMs() const` | Returns active ON duration in ms. |
| `uint32_t getOffTimeMs() const` | Returns active OFF duration in ms. |
| `void reset()` | Forces power to 0.0 % and state to OFF. |

### `output::OutputManager`

| Method | Description |
| :--- | :--- |
| `explicit OutputManager(const OutputSystemConfig&)` | Constructor with optional custom pin/polarity mapping. |
| `bool begin()` | Initializes all 5 GPIO pins to safe OFF state via `driver/gpio.h`. |
| `void setSsrTop(bool on)` | Commands Top SSR state (suppressed if inhibited or simulated). |
| `void setSsrBottom(bool on)` | Commands Bottom SSR state (suppressed if inhibited or simulated). |
| `void setFan(bool on)` | Commands Cooling Fan output state. |
| `void setLamp(bool on)` | Commands Inspection Lamp output state. |
| `void setBuzzer(bool on)` | Commands Acoustic Buzzer output state. |
| `void setInhibit(bool inhibit)` | Engages or releases Safety Inhibit hardware lockout (atomic). |
| `bool isInhibited() const` | Returns `true` if Safety Inhibit lockout is active. |
| `void setSimulationMode(bool sim)` | Engages or releases Simulation Mode hardware lockout (atomic). |
| `bool isSimulationMode() const` | Returns `true` if Simulation Mode is active. |
| `bool getSsrTopState() const` | Returns logical active state for Top SSR. |
| `bool getSsrBottomState() const` | Returns logical active state for Bottom SSR. |
| `bool getFanState() const` | Returns physical active state for Cooling Fan. |
| `bool getLampState() const` | Returns physical active state for Work Lamp. |
| `bool getBuzzerState() const` | Returns physical active state for Buzzer. |
| `void setChannelPolarity(gpio_num_t pin, bool activeHigh)` | Reconfigures active-HIGH vs. active-LOW inverted logic at runtime. |

---

## 8. Unit Tests & Automated Verification

The component includes 100 % automated test coverage in [`test/test_output.cpp`](test/test_output.cpp):

1. **`test_burst_fire_zero`**: Confirms 0 % power yields 0 ms ON, 1000 ms OFF, state OFF.
2. **`test_burst_fire_hundred`**: Confirms 100 % power yields 1000 ms ON, 0 ms OFF, state ON.
3. **`test_burst_fire_clamping`**: Confirms power inputs are clamped to `[0.0, 100.0]`.
4. **`test_burst_fire_window_clamping`**: Confirms window duration is clamped to `[500, 4000]` ms.
5. **`test_burst_fire_timing_calc`**: Verifies exact duty cycle math (e.g. 30 % = 300 ms ON, 700 ms OFF).
6. **`test_burst_fire_reset`**: Verifies reset immediately clears power and state to OFF.
7. **`test_output_safety_inhibit`**: Confirms `setInhibit(true)` strictly suppresses SSR states.
8. **`test_output_simulation_mode`**: Confirms simulation mode locks SSR states OFF.
9. **`test_output_auxiliary_channels`**: Validates Fan, Lamp, and Buzzer state toggling.
10. **`test_output_channel_polarity`**: Verifies dynamic active-HIGH / active-LOW polarity reconfiguration.

---

## 9. CMake Integration

Defined in [`CMakeLists.txt`](CMakeLists.txt):

```cmake
idf_component_register(
    SRCS "output_manager.cpp" "burst_fire.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config driver esp_driver_gpio esp_timer
    PRIV_REQUIRES log
)
```

---

## 10. 📜 License

Copyright (C) 2026 EM-OpenTech

This component is part of the BGA Reflow Controller project and is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)**.

* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `AGPL-3.0-or-later`

See the project root [`LICENSE`](../../LICENSE) file for the full license text.
