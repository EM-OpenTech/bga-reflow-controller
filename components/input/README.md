# Input & Front-Panel Hardware Manager Component (C++20)

Hardware GPIO input manager and non-blocking software debouncer for the **BGA Reflow Controller**, running natively on **ESP-IDF v6.0.2** and **FreeRTOS**.

This component manages all physical front-panel controls, distinguishing between momentary push-buttons (Start, Stop/E-Stop) and latching toggle switches (Fan Override, Lamp Override) with microsecond timing accuracy.

---

## 1. Overview & Architecture

Physical front-panel controls on industrial soldering and rework stations operate in harsh, electrically noisy environments near 230V zero-crossing SSRs and high-current heating elements. The `input` component provides robust, non-blocking digital input signal conditioning:

```
    [ Start Button ]      [ Stop Button ]      [ Fan Switch ]      [ Lamp Switch ]
         GPIO 1                GPIO 2               GPIO 3              GPIO 8
           |                     |                    |                   |
           +---------------------+--------------------+-------------------+
                                          |
                                          v
    +-----------------------------------------------------------------------------+
    |                             input::InputManager                             |
    |  - Hardware GPIO HAL (driver/gpio.h)                                        |
    |  - Active-LOW / Active-HIGH Switching Logic                                 |
    |  - Internal 45 kΩ Pull-Up / Pull-Down Resistors                             |
    |  - Non-Blocking Microsecond Debounce Filter (esp_timer_get_time)            |
    |  - Consume-on-Read Event Latches & Callback Dispatch                        |
    +-----------------------------------------------------------------------------+
                                          |
                                          v  (Polled at 50 Hz / 20 ms)
    +-----------------------------------------------------------------------------+
    |                         input_task (Core 0, 50 Hz)                          |
    |  - Short Press (50 ms..2 s) vs. Long Press (> 2 s) Duration Evaluation      |
    |  - Latching Override Propagation to Reflow FSM                             |
    |  - Cross-Core Event Dispatch (fsmCmdPost -> Core 1)                         |
    +-----------------------------------------------------------------------------+
```

---

## 2. Hardware Pinout & Default Channel Configuration

> [!NOTE]
> **Hardware Dependency (Example Configuration):**  
> The table below illustrates the default reference configuration. The actual polarity (`activeLow`) and hardware bias resistors depend directly on the **wiring harness and switch types utilized**:
> * **Standard Front-Panel Switches (Switching to GND):** Typically operate as **Active-LOW** (pressing the button connects the GPIO to `0V` / Ground). The internal 45 kΩ pull-up resistor holds the pin stably at `3.3V` while open.
> * **Active-HIGH Switching (Switching to +3.3V):** If switches connect the pin to VCC when closed, configure `activeLow = false` and enable internal pull-down (`GPIO_PULLDOWN_ENABLE`).
> * **High-EMI Environments (Long Wire Harnesses > 20 cm):** For maximum noise immunity near 230V heating elements and AC SSRs, install an external hardware pull-up resistor (4.7 kΩ or 10 kΩ to 3.3V) and an optional 100 nF ceramic capacitor to GND (RC low-pass filter).

Defined centrally in `config::PinConfig` (`components/config/include/config/pin_config.hpp`):

| Channel | GPIO Pin | Default Polarity (Reference) | Hardware Bias | Switch Type & Function |
| :--- | :---: | :---: | :---: | :--- |
| **Start Button** | `GPIO_NUM_1` | **Active-LOW** (`true`) | Pull-Up Enabled | Momentary tactile push-button (Preheat / Reflow Start) |
| **Stop Button** | `GPIO_NUM_2` | **Active-LOW** (`true`) | Pull-Up Enabled | Momentary tactile push-button (Abort / Reset / Skip Step) |
| **Fan Switch** | `GPIO_NUM_3` | **Active-LOW** (`true`) | Pull-Up Enabled | Latching toggle switch (Forced-Air Cooling Fan manual override) |
| **Lamp Switch** | `GPIO_NUM_8` | **Active-LOW** (`true`) | Pull-Up Enabled | Latching toggle switch (Inspection LED Lamp manual override) |

---

## 3. Input Types & Debouncing Behavior

The `input` component provides two distinct debouncing and event-generation pipelines:

### A. Momentary Push-Buttons (`updateButton`)
* **Leading-Edge Event Trigger**: Registers a click event (`wasPressedEvent = true`) strictly when transitioning from released to pressed after the debounce window has elapsed.
* **Consume-on-Read Semantics**: Calling `wasStartPressed()` or `wasStopPressed()` returns `true` once and immediately resets the flag to `false`.
* **Continuous Hold State**: `isStartHeld()` and `isStopHeld()` return the current debounced physical contact state (`true` while button is held down), enabling multi-duration press logic.

### B. Latching Toggle Switches (`updateSwitch`)
* **Dual-Edge State Tracking**: Evaluates permanent contact closures (e.g. 2-position toggle switches).
* **Continuous State Query**: `isFanSwitchActive()` and `isLampSwitchActive()` query whether the switch is physically closed (`true`) or open (`false`).
* **State Change Callbacks**: `setOnFanSwitchChanged(cb)` and `setOnLampSwitchChanged(cb)` execute whenever the switch flips between ON and OFF.

### C. Non-Blocking Debounce Algorithm
* Uses 64-bit microsecond timestamps via `esp_timer_get_time()`.
* Default debounce duration: **`DEFAULT_DEBOUNCE_MS = 30` ms**.
* If a contact bounces during the transition, the internal timestamp (`lastStateChangeUs`) is re-armed. The state only updates once the signal remains continuously stable for the configured debounce duration.

---

## 4. FreeRTOS Integration (`input_task`, Core 0, 50 Hz)

In the firmware architecture, physical inputs are sampled inside `inputTask` on Core 0 running at **50 Hz (20 ms period)** via `vTaskDelayUntil`:

### Duration-Sensitive Button Actions:

| Input Channel | Press Duration | System Context | FSM Command Posted to Core 1 |
| :--- | :---: | :--- | :--- |
| **Start Button** | **Long Press (> 2.0 s)** | IDLE / DONE | `FsmCommandType::PREHEAT` (Loads active profile and starts preheat) |
| **Start Button** | **Short Press (50 ms..2.0 s)** | PREHEAT (Plateau reached) | `FsmCommandType::REFLOW` (Advances to reflow ramp) |
| **Stop Button** | **Long Press (> 2.0 s)** | Active Process | `FsmCommandType::STOP` (Aborts process into cooling) |
| **Stop Button** | **Short Press (< 2.0 s)** | FAULT State | `FsmCommandType::RESET_FAULT` (Clears safety alarms) |
| **Stop Button** | **Short Press (< 2.0 s)** | PREHEAT / SOAK / REFLOW | `FsmCommandType::SKIP_STEP` (Advances phase immediately) |

---

## 5. Usage Example (ESP-IDF C++)

```cpp
#include "input/input_manager.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "InputApp";

input::InputManager inputs;

void app_main() {
    // 1. Initialize input GPIOs with pull-ups safely
    inputs.begin();

    // 2. Register optional event callbacks
    inputs.setOnStartPressed([]() {
        ESP_LOGI(TAG, "Start Button clicked!");
    });

    inputs.setOnFanSwitchChanged([](bool active) {
        ESP_LOGI(TAG, "Fan toggle switch is now: %s", active ? "ON" : "OFF");
    });

    // 3. Periodic input task loop (50 Hz / 20 ms):
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(20);

    while (true) {
        // Poll and debounce physical signals
        inputs.update();

        // Alternatively check consume-on-read latches:
        if (inputs.wasStopPressed()) {
            ESP_LOGW(TAG, "Stop button was pressed!");
        }

        vTaskDelayUntil(&lastWakeTime, period);
    }
}
```

---

## 6. API Reference

### `input::InputManager`

| Method | Description |
| :--- | :--- |
| `explicit InputManager(const InputSystemConfig&)` | Constructor with optional custom pin, polarity, and debounce configuration. |
| `bool begin()` | Configures all GPIO input pins and pull resistors via `driver/gpio.h`. |
| `void update()` | Polls physical GPIO levels and updates debounce state machines. Call periodically (e.g. 20 ms). |
| `bool wasStartPressed()` | Returns `true` if Start button was pressed since last call (consume-on-read). |
| `bool wasStopPressed()` | Returns `true` if Stop button was pressed since last call (consume-on-read). |
| `bool isStartHeld() const` | Returns `true` if Start button is currently physically held down. |
| `bool isStopHeld() const` | Returns `true` if Stop button is currently physically held down. |
| `void setOnStartPressed(ButtonCallback cb)` | Registers callback executed on Start button press event. |
| `void setOnStopPressed(ButtonCallback cb)` | Registers callback executed on Stop button press event. |
| `bool isFanSwitchActive() const` | Returns `true` if Fan toggle switch is in the active/closed position. |
| `bool isLampSwitchActive() const` | Returns `true` if Lamp toggle switch is in the active/closed position. |
| `void setOnFanSwitchChanged(SwitchCallback cb)` | Registers callback executed whenever Fan switch changes state. |
| `void setOnLampSwitchChanged(SwitchCallback cb)` | Registers callback executed whenever Lamp switch changes state. |

---

## 7. Unit Tests & Automated Verification

The component includes 100 % automated test coverage in [`test/test_input.cpp`](test/test_input.cpp):

1. **`test_input_config_defaults`**: Verifies default pin assignments, Active-LOW polarity, and 30 ms debounce timing.
2. **`test_input_active_high_and_custom_config`**: Verifies custom Active-HIGH logic and custom debounce windows.
3. **`test_input_callbacks`**: Verifies registration and execution of Start, Stop, and Fan switch callbacks.
4. **`test_input_lamp_switch_callback`**: Verifies registration and execution of Lamp switch callbacks.
5. **`test_input_initial_states`**: Verifies that initial debounced states start inactive with no spurious triggers.
6. **`test_input_was_pressed_consume_semantics`**: Validates consume-on-read latch resetting for button events.

---

## 8. CMake Integration

Defined in [`CMakeLists.txt`](CMakeLists.txt):

```cmake
idf_component_register(
    SRCS "input_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_gpio esp_timer config
    PRIV_REQUIRES log
)
```

---

## 9. 📜 License

Copyright (C) 2026 EM-OpenTech

This component is part of the BGA Reflow Controller project and is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)**.

* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `AGPL-3.0-or-later`

See the project root [`LICENSE`](../../LICENSE) file for the full license text.
