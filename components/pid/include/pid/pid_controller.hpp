/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file pid_controller.hpp
 * @brief High-level closed-loop PID temperature controller.
 *
 * Wraps the QuickPID regulation engine tailored for infrared ceramic and quartz heaters.
 * Features Proportional-on-Measurement (pOnMeas), Derivative-on-Measurement (dOnMeas),
 * Anti-Windup Clamping (iAwClamp), and deterministic 10 Hz FreeRTOS task synchronization.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include <cstdint>
#include "QuickPID.hpp"

namespace pid {

// ============================================================================
// PID CONTROLLER CONSTANTS
// ============================================================================

/**
 * @brief Fixed PID Sample Time (100 ms / 100,000 µs = 10 Hz Control Loop).
 * 
 * [DO NOT CHANGE]:
 * This value is fixed and hardcoded to exactly 100 ms (0.1 s).
 * In ESP-IDF, timing is exclusively governed by the FreeRTOS `control_task` 
 * running on Core 1 at 10 Hz via `vTaskDelayUntil`.
 * QuickPID operates in `Control::timer` mode to synchronize deterministically
 * with this 100ms task cycle.
 * 
 * [IMPORTANT]:
 * If the FreeRTOS `control_task` cycle time is ever modified (e.g. from 100ms to 200ms), 
 * `FIXED_PID_SAMPLE_TIME_MS` MUST be updated to match the exact same cycle time.
 * QuickPID uses this sample time internally inside `SetTunings(Kp, Ki, Kd)` to scale:
 *   ki_internal = Ki * (sampleTimeSec)   [e.g. Ki * 0.1s]
 *   kd_internal = Kd / (sampleTimeSec)   [e.g. Kd / 0.1s]
 * Matching this ensures that standard physical PID gains (from sTune autotuner or manual)
 * are always scaled 100% mathematically correct.
 * 
 * Autotuning (`sTune`) uses its own independent microsecond timing via `esp_timer_get_time()`.
 */
constexpr uint32_t FIXED_PID_SAMPLE_TIME_MS = 100;
constexpr uint32_t FIXED_PID_SAMPLE_TIME_US = FIXED_PID_SAMPLE_TIME_MS * 1000;

/// Default minimum heater output power in percent (%)
constexpr float DEFAULT_PID_OUTPUT_MIN = 0.0f;

/// Default maximum heater output power in percent (%)
constexpr float DEFAULT_PID_OUTPUT_MAX = 100.0f;

/**
 * @class PIDController
 * @brief High-level closed-loop PID temperature controller.
 * 
 * Configured specifically for high thermal inertia systems (open-air ceramic
 * infrared heaters) to prevent overshoots and derivative spikes during step transitions.
 * 
 * Features for Ceramic Heater Reflow:
 *   - Proportional on Measurement (pOnMeas): Eliminates setpoint step spikes
 *   - Derivative on Measurement (dOnMeas): Prevents derivative kicks on ramp steps
 *   - Anti-Windup Clamping (iAwClamp): Clamps integral term during slow thermal lag
 *   - Deterministic 10 Hz FreeRTOS task synchronization (Control::timer mode)
 *
 * CROSS-REFERENCED with:
 *   - config::MachineSettings  (machine_config.hpp) -> topKp/Ki/Kd, bottomKp/Ki/Kd
 *   - config::PidLibrary       (machine_config.hpp) -> Gain Scheduling Interpolation
 *   - pid::PidAutotuner        (pid_autotuner.hpp)  -> Tuning Results Destination
 *   - main::AppController      (app_controller.hpp) -> Synchronous Control Task
 */
class PIDController {
public:
    /**
     * @brief Construct a new PIDController instance.
     * 
     * @param kp Proportional gain (default: 2.0f)
     * @param ki Integral gain (default: 0.05f)
     * @param kd Derivative gain (default: 1.0f)
     */
    PIDController(float kp = 2.0f, float ki = 0.05f, float kd = 1.0f);

    /**
     * @brief Initialize the PID controller parameters and output limits.
     * Operates strictly on a fixed 100ms FreeRTOS cycle.
     */
    void begin();

    /**
     * @brief Update PID tuning parameters dynamically during runtime (Gain Scheduling).
     * @param kp New proportional gain
     * @param ki New integral gain
     * @param kd New derivative gain
     */
    void setTunings(float kp, float ki, float kd);

    /**
     * @brief Set target temperature setpoint in °C.
     * @param setpoint Target temperature in °C
     */
    void setSetpoint(float setpoint);

    /**
     * @brief Provide latest measured temperature input in °C.
     * @param input Current filtered process temperature in °C
     */
    void setInput(float input);

    /**
     * @brief Calculate PID output. Should be called periodically in the control loop.
     * @return true if a new PID calculation was computed, false if sample time hasn't elapsed.
     */
    bool compute();

    /**
     * @brief Reset PID integral sum and history (bumpless transfer).
     */
    void reset();

    /**
     * @brief Enable or disable automatic PID regulation.
     * @param enabled true for Automatic PID mode, false for Manual mode.
     */
    void setAutomatic(bool enabled);

    /**
     * @brief Set manual power output override in percent (0.0% to 100.0%).
     * @param percent Output power in percent (clamped to configured limits)
     */
    void setManualOutput(float percent);

    /**
     * @brief Set output min/max limits in percent (default: 0.0% to 100.0%).
     * @param minPercent Minimum output limit in percent
     * @param maxPercent Maximum output limit in percent
     */
    void setOutputLimits(float minPercent, float maxPercent);

    /**
     * @brief Get fixed cycle time in milliseconds (100 ms).
     * @return Fixed sample period in ms
     */
    uint32_t getSampleTimeMs() const { return FIXED_PID_SAMPLE_TIME_MS; }

    // Query methods
    /**
     * @brief Check whether the controller is in automatic mode.
     * @return true if automatic mode is active
     */
    bool  isAutomatic() const { return _automatic; }

    /**
     * @brief Get current target setpoint temperature.
     * @return Setpoint in °C
     */
    float getSetpoint()  const { return _setpoint; }

    /**
     * @brief Get last supplied process temperature input.
     * @return Input temperature in °C
     */
    float getInput()     const { return _input; }

    /**
     * @brief Get current computed output power.
     * @return Output power in percent (0.0% - 100.0%)
     */
    float getOutput()    const { return _output; }
    
    /**
     * @brief Get active proportional gain Kp.
     * @return Kp gain
     */
    float getKp() const { return _kp; }

    /**
     * @brief Get active integral gain Ki.
     * @return Ki gain
     */
    float getKi() const { return _ki; }

    /**
     * @brief Get active derivative gain Kd.
     * @return Kd gain
     */
    float getKd() const { return _kd; }

    // Detailed term diagnostic inspection (P, I, D breakdown)
    /**
     * @brief Get proportional component contribution of output.
     * @return P-term value
     */
    float getPterm()     { return _quickPid.GetPterm(); }

    /**
     * @brief Get integral component contribution of output.
     * @return I-term value
     */
    float getIterm()     { return _quickPid.GetIterm(); }

    /**
     * @brief Get derivative component contribution of output.
     * @return D-term value
     */
    float getDterm()     { return _quickPid.GetDterm(); }

    /**
     * @brief Get total internal output summation.
     * @return Output sum value
     */
    float getOutputSum() { return _quickPid.GetOutputSum(); }

private:
    float _input    = 0.0f;  ///< Current filtered process temperature input (°C)
    float _output   = 0.0f;  ///< Computed control output power percentage (0.0% - 100.0%)
    float _setpoint = 0.0f;  ///< Active target setpoint temperature (°C)

    float _kp = 2.0f;        ///< Proportional gain
    float _ki = 0.05f;       ///< Integral gain
    float _kd = 1.0f;        ///< Derivative gain

    bool _automatic = false; ///< Regulation mode (true = Auto PID, false = Manual)

    QuickPID _quickPid;      ///< Underlying QuickPID calculation engine instance
};

} // namespace pid
