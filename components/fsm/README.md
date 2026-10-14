# Reflow Finite State Machine Component (C++20)

Dual-channel BGA reflow process orchestrator, step sequencing engine, temperature settle-gate manager, and dynamic gain scheduling supervisor for the **BGA Reflow Controller**, running natively on **ESP-IDF v6.0.2** and **FreeRTOS**.

This component manages the complete thermal profile lifecycle across two independent heating elements (Top ceramic/nozzle heater and Bottom infrared pre-heater), enforcing strict metallurgical timing requirements, Time Above Liquidus (TAL) accumulation, hardware safety overrides, and acoustic process feedback.

---

## 1. Overview & Architecture

BGA rework stations require strict thermal profiling to prevent thermal shock, PCB warpage, delamination, and solder joint voids. The `fsm` component acts as the supervisory orchestrator between input events, closed-loop PID controllers, hardware SSR actuators, and thermal safety watchdogs:

```
    [ Physical Inputs / Web REST API ]          [ SafetyWatchdog / Hardware E-Stop ]
          (Start / Stop / Skip / Abort)                           (Thermal Fault)
                       |                                                 |
                       +-----------------------+-------------------------+
                                               |
                                               v
    +-----------------------------------------------------------------------------------------+
    |                                     fsm::ReflowFSM                                      |
    |  - State Machine Lifecycle (IDLE -> PREHEAT -> SOAK -> REFLOW -> COOLING -> DONE)       |
    |  - Independent Dual-Channel Step Runners (Top & Bottom Heaters)                         |
    |  - Settle-Gate Temperature Stability Windows & Hold Time Deduction                      |
    |  - Cumulative Time Above Liquidus (TAL >= 217°C) Tracking                               |
    |  - Piecewise Linear Dynamic PID Gain Scheduling Interpolation                           |
    |  - Automated Fan Cooling Sequencing with Hardware Manual Override OR-Logic             |
    |  - Acoustic Buzzer Notification Generator (Preheat Done, Reflow Complete)               |
    +-----------------------------------------------------------------------------------------+
                       |                                                 |
                       v                                                 v
         [ pid::PIDController Top/Bot ]                    [ output::OutputManager ]
         - Real-time setpoints (RAMP/HOLD)                 - SSR Hardware Inhibit (Emergency)
         - Interpolated Kp, Ki, Kd tunings                 - 24V Cooling Fan & Lamp Relays
         - Open-loop step tests (AUTOTUNE)                 - Acoustic Alert Buzzer
```

---

## 2. State Machine Lifecycle

The process follows industrial dual-stage BGA rework standards: the Bottom pre-heater brings the entire board substrate to a uniform preheat plateau to prevent thermal mass sink effects, after which the Top heater executes the active reflow curve.

```
       +-----------------------------------------------------------------------+
       |                                                                       |
       v                                                                       |
   [ IDLE ]  <------------------------+                                        |
      |                               |                                        |
      | (startPreheat)                |                                        |
      v                               |                                        |
 [ PREHEAT ]                          |                                        |
   Bottom: Ramps & Holds              |                                        |
   Top:    Locked at 0% power         | (DONE hold timeout: 3s)                |
      |                               |                                        |
      | (Preheat done buzzer /        |                                        |
      |  startReflow triggered)       |                                        |
      v                               |                                        |
  [ SOAK ]                            |                                        |
   Bottom: Holds preheat temp         |                                        |
   Top:    Ramps towards liquidus     |                                        |
      |                               |                                        |
      | (Top temp >= 217°C)           |                                        |
      v                               |                                        |
 [ REFLOW ]                           |                                        |
   Bottom: Continues holding          |                                        |
   Top:    Executes peak reflow       |                                        |
      |                               |                                        |
      | (All Top steps finished)      |                                        |
      v                               |                                        |
 [ COOLING ] -------------------> [ DONE ]                                     |
   Heaters: Both OFF                Buzzer: Active (3s)                        |
   Fan:     ON after delay          Sets preheatDone = false                   |
   Wait:    Bot <= safeTemp                                                    |
                                                                               |
   =========================== SPECIAL STATES ===============================  |
   [ ANY ACTIVE STATE ] ----(triggerFault / SafetyWatchdog)----> [ FAULT ]     |
   Heaters: Inhibited immediately (0%)                            SSR Inhibit  |
   Fan:     ON if Top or Bot > coolingSafeTemp                    Clear via    |
                                                                  STOP click   |
   [ IDLE / DONE ] ---------(startAutotune)--------------------> [ AUTOTUNE ]  |
   Executes sTan / sTune step response test; returns to COOLING                |
                                                                               |
   [ IDLE / DONE ] ---------(enterBackupState)-----------------> [ BACKUP ] ---+
   Locks heaters at 0% during JSON backup operations
```

### State Definitions (`fsm::ReflowState`)

| State Enum | Code | Channel Behavior & Actuators | Exit Condition |
| :--- | :---: | :--- | :--- |
| `IDLE` | `0` | Both heaters 0%. Fan and lamp available via manual override. | `startPreheat()`, `startAutotune()`, or `enterBackupState()`. |
| `PREHEAT` | `1` | Bottom heater processes its profile steps. Top heater is hard-locked at 0% output. | All bottom steps finished. Bottom holds indefinitely until user triggers `startReflow()`. |
| `SOAK` | `2` | Bottom heater holds the last preheat temperature. Top heater processes steps below 217.0 °C. | Top temperature reaches $\ge 217.0\text{ }^\circ\text{C}$ (auto-transitions to `REFLOW`). |
| `REFLOW` | `3` | Bottom heater holds preheat temperature. Top heater processes peak steps. | All Top heater profile steps completed. |
| `COOLING` | `4` | Both heaters 0%. Cooling fan starts after `fanCoolingDelayS` and runs for at least `fanCoolingDurationS`. | `fanRunMs >= fanCoolingDurationS` **AND** `bottomTemp <= coolingSafeTemp`. |
| `DONE` | `5` | Both heaters 0%. Buzzer sounds continuous notification for 3000 ms. | Automatically transitions to `IDLE` after `DONE_HOLD_SEC` (3 s). |
| `FAULT` | `6` | Emergency shutdown: SSR inhibit active. Fan forced ON if temperature $> \text{coolingSafeTemp}$. | Manual reset via physical STOP push-button (`resetFault()`). |
| `AUTOTUNE` | `7` | Single heater open-loop step test using integrated `pid::PidAutotuner`. Opposite heater locked at 0%. | Test finished or aborted via `stopAutotune()`; enters `COOLING`. |
| `BACKUP` | `8` | Standby mode locking heating elements to 0% during LittleFS backup operations. | `exitBackupState()` returns to `IDLE`. |

---

## 3. Settle-Gate & Step Execution Lifecycle

Each profile step in `config::ReflowProfile` contains:
* `temp`: Target setpoint (°C)
* `ramp`: Heating ramp rate (°C/s)
* `time`: Total hold duration (seconds)

```
 Temperature (°C)
       ^
Target | - - - - - - - - - - +---------------+ - - - - -  Upper Tolerance (+holdHighTolerance)
       |                    /|   Tolerance   |
       |                   / |     Band      |
       |                  /  +---------------+ - - - - -  Lower Tolerance (-holdLowTolerance)
       |                 /   |               |
       |                /    |<-- Settle --->|<-- Remaining -->|
       |               /     |    Time (s)   |    Hold Time    |
       |              /      |               |                 |
       |             /       |               |                 |
 Ambient +------------+-------+---------------+-----------------+--------> Time
           [ RAMPING ]        [  SETTLING   ] [    HOLDING    ]
                                  Gate
```

### Step Runner Execution Phases
1. **Ramping (`isRamping = true`):**
   The internal setpoint interpolates smoothly from the start temperature towards `step.temp` at the rate of `step.ramp` °C/s. When the setpoint reaches the target temperature, the runner transitions to **Settling**.
2. **Settle-Gate (`isSettling = true`):**
   Industrial thermal processes require the physical workpiece and ceramic plate to reach thermodynamic equilibrium before the hold soak timer starts.
   * The actual thermocouple temperature must enter the tolerance band:  
     $$\left[\text{step.temp} - \text{holdLowTolerance},\; \text{step.temp} + \text{holdHighTolerance}\right]$$
   * The temperature must remain inside this window continuously for `settleTimeS` (configured in `MachineSettings`).
   * **Excursion Reset:** If the temperature drifts outside the window before the timer finishes, `settleAccumMs` resets to 0.
3. **Hold Deduction Zero-Drift Optimization:**
   To prevent artificial profile elongation, the time spent settling is deducted from the planned hold duration:  
   $$\text{Remaining Hold Time} = \max\left(0,\; \text{step.time} - \text{settings.settleTimeS}\right)$$
4. **Holding (`isHolding = true`):**
   The controller maintains `step.temp` until the remaining hold time decrements to zero, then automatically advances to the next step via `advanceStep()`.

---

## 4. Time Above Liquidus (TAL) & Chart Markers

### 4.1 Time Above Liquidus (TAL)
For standard lead-free SAC305 solder, the liquidus melting point is $217.0\text{ }^\circ\text{C}$ (`REFLOW_ZONE_MIN_TEMP`).
* In states `SOAK`, `REFLOW`, and `COOLING`, whenever `topTemp >= 217.0f`, the internal accumulator `_talAccumMs` increments by `dtMs`.
* Queried via `fsm.getTalSec()`.
* Preserved in `DONE` and `IDLE` after completion so telemetry graphs and quality inspection reports can inspect final TAL.

### 4.2 Step Markers (`fsm::StepMarker`)
The moment a step enters its **Hold** phase, an immutable marker is recorded into `_stepMarkers` (capacity: 20 markers, sized for full $10+10$ dual-channel profiles):
```cpp
struct StepMarker {
    uint32_t timeS;      // Elapsed process seconds when hold started
    uint8_t  stepIndex;  // Profile step index (0-based)
    float    targetTemp; // Target plateau temperature (°C)
    bool     isTop;      // true = Top heater, false = Bottom heater
};
```
These markers are streamed over WebSocket to the Web UI to render vertical event lines on the reflow telemetry chart.

---

## 5. Dynamic PID Gain Scheduling

Reflow ovens exhibit highly non-linear thermal dynamics (radiative heat transfer dominates at higher temperatures, altering system gain). The FSM includes built-in piecewise linear interpolation:

1. **Step Transition Gain Lookup:**  
   When entering a new step, `applyPidGains(step.temp, isTop)` evaluates `config::PidLibrary`.
2. **Piecewise Linear Interpolation & Zero-Division Protection:**  
   Given points $(T_1, K_1)$ and $(T_2, K_2)$ bracketing target temperature $T$:
   $$K(T) = K_1 + \frac{T - T_1}{T_2 - T_1} \cdot (K_2 - K_1)$$
   * `setPidLibrary()` automatically sorts tuning points ascending by temperature via `std::sort`.
   * Division-by-zero is prevented if adjacent points share identical temperatures ($\Delta T \le 0.001\text{ }^\circ\text{C}$).
3. **Fallback & Arithmetic Protection:**  
   If the PID library is disabled (`pidLibraryEnabled = false`), empty, contains `NaN`/`Inf` parameters, or yields non-positive gains ($K_p \le 0$), the FSM automatically falls back to baseline gains defined in `MachineSettings`.

---

## 6. Safety, Override & Input Handling

| Event / Trigger | Source | Action Executed by FSM |
| :--- | :--- | :--- |
| **Start (Long Press > 2s)** | Front-Panel Button / REST API | Calls `startPreheat(profile)` from `IDLE` or `DONE`. |
| **Start (Short Press < 2s)** | Front-Panel Button / REST API | Calls `startReflow()`. Valid only in `PREHEAT` when `isPreheatDone() == true`. |
| **Stop (Short Press < 2s)** | Front-Panel Button / REST API | **Process Active:** Calls `skipStep()` (advances active channel to next step).<br>**State FAULT:** Calls `resetFault()` (clears inhibit, returns to `IDLE`). |
| **Stop (Long Press > 2s)** | Front-Panel Button / REST API | Calls `stop()`: Aborts process gracefully and enters `COOLING`. |
| **Safety Violation** | `safety::SafetyWatchdog` | Calls `triggerFault()`: Inhibit enabled (heaters forced to 0%), fan forced ON if $> \text{coolingSafeTemp}$. |
| **Fan Override Switch** | Front-Panel Toggle / UI | `setFanOverride(bool)`: OR'd with automatic FSM cooling fan logic (`_fanAuto \|\| _fanOverride`). |
| **Lamp Override Switch** | Front-Panel Toggle / UI | `setLampOverride(bool)`: Directly controls inspection LED output in all states. |

---

## 7. C++20 Usage Example

```cpp
#include "fsm/reflow_fsm.hpp"
#include "output/output_manager.hpp"
#include "pid/pid_controller.hpp"
#include "config/machine_config.hpp"
#include "esp_log.h"

static const char* TAG = "FSM_APP";

void run_reflow_process()
{
    // 1. Dependency setup
    config::MachineSettings settings;
    output::OutputManager outputs;
    outputs.init();

    pid::PIDController topPid;
    pid::PIDController bottomPid;
    topPid.begin();
    bottomPid.begin();

    // 2. Instantiate Reflow FSM
    fsm::ReflowFSM fsm(settings, outputs, topPid, bottomPid);

    // 3. Define Reflow Profile (SAC305 Lead-Free)
    config::ReflowProfile profile;
    profile.name = "Lead-Free BGA SAC305";
    profile.stepsBottom.push_back({150.0f, 60, 1.0f}); // Preheat: 150°C, 60s hold
    profile.stepsTop.push_back({170.0f, 30, 1.5f});    // Soak: 170°C, 30s hold
    profile.stepsTop.push_back({235.0f, 20, 2.0f});    // Reflow: 235°C, 20s hold

    // 4. Start Preheat
    if (fsm.startPreheat(profile)) {
        ESP_LOGI(TAG, "Preheat initiated successfully");
    }

    // 5. Periodic execution in FreeRTOS control_task (200 ms / 5 Hz loop)
    float currentTopTemp = 25.0f;
    float currentBottomTemp = 25.0f;
    uint32_t dtMs = config::Timing::CONTROL_LOOP_PERIOD_MS;

    while (fsm.getState() != fsm::ReflowState::IDLE) {
        // Feed sensor readings into FSM
        fsm.update(currentTopTemp, currentBottomTemp, dtMs);

        // Compute closed-loop PID controls
        topPid.compute();
        bottomPid.compute();

        // If preheat finished, trigger reflow
        if (fsm.getState() == fsm::ReflowState::PREHEAT && fsm.isPreheatDone()) {
            ESP_LOGI(TAG, "Preheat complete, transitioning to SOAK/REFLOW");
            fsm.startReflow();
        }

        vTaskDelay(pdMS_TO_TICKS(dtMs));
    }
}
```

---

## 8. Unit Tests & Automated Verification

The component includes 17 exhaustive automated Unity test cases located in [`test/test_fsm.cpp`](test/test_fsm.cpp) validating every state transition, edge case, and safety behavior:

| Test Case | Description |
| :--- | :--- |
| `test_fsm_initial_state` | Verifies initial `IDLE` state, setpoints zeroed, and preheat flags cleared. |
| `test_fsm_start_preheat` | Tests transition to `PREHEAT` and setpoint initialization. |
| `test_fsm_stop` | Verifies graceful abortion transitions into `COOLING`. |
| `test_fsm_overrides` | Confirms manual fan and lamp overrides operate independently in all states. |
| `test_fsm_full_profile_simulation` | Simulates an entire multi-step profile against the mock thermal simulator. |
| `test_fsm_pid_library_gain_scheduling` | Verifies gain interpolation across multiple temperature points. |
| `test_fsm_tal_accumulation` | Verifies TAL accumulation occurs strictly while `topTemp >= 217.0°C`. |
| `test_fsm_fault_and_reset` | Tests emergency shutdown, SSR inhibit latching, and manual reset to `IDLE`. |
| `test_fsm_skip_step` | Validates premature step advance via short STOP push-button. |
| `test_fsm_settle_gate_deduction` | Asserts settle time is correctly deducted from the remaining hold duration. |
| `test_fsm_buzzer_pulse_behavior` | Verifies preheat (1s) and reflow done (3s) buzzer timing pulses. |
| `test_fsm_autotune_state_transition` | Verifies open-loop autotune sequencing and return to cooling. |
| `test_fsm_backup_state_lock` | Asserts heaters remain 0% and starts are rejected during configuration backup. |
| `test_fsm_empty_profile_rejection` | Rejects invalid profiles with empty step arrays. |
| `test_fsm_emergency_stop_under_power` | Asserts immediate heater cut-off and fan activation when stopped while hot. |
| `test_fsm_tolerance_excursion_pause` | Verifies settle accumulator resets when temperature leaves tolerance band. |
| `test_fsm_end_to_end_600s_simulation` | Full fast-forward 10-minute simulation of a dual-channel SAC305 profile. |

---

## 9. CMake Integration

Defined in `components/fsm/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "reflow_fsm.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config output pid
    PRIV_REQUIRES esp_timer log
)
```

To consume this component in another module:

```cmake
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "include"
    REQUIRES fsm
)
```

---

## 10. 📜 License

Copyright (C) 2026 EM-OpenTech

This component is part of the BGA Reflow Controller project and is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)**.

* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `AGPL-3.0-or-later`

See the project root [`LICENSE`](../../LICENSE) file for the full license text.
