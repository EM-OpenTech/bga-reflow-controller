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
 * @file pid_autotuner.hpp
 * @brief High-level open-loop inflection point PID autotuner wrapper.
 *
 * Wraps the sTune inflection point autotuning engine for ceramic and quartz
 * infrared heaters. Configured to run deterministically from the 200ms
 * FreeRTOS control task loop on Core 1 (config::Timing::CONTROL_LOOP_PERIOD_MS).
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "pid_controller.hpp"
#include "sTune.hpp"
#include <cstdint>

namespace pid {

/**
 * @brief Translates an sTune TuningMethod enum to its human-readable algorithm name.
 * @param method TuningMethod enum value
 * @return String literal representation (e.g. "Mixed_PID")
 */
[[nodiscard]] constexpr const char* getTuningMethodName(sTune::TuningMethod method) {
    switch (method) {
    case sTune::ZN_PID:
        return "ZN_PID";
    case sTune::DampedOsc_PID:
        return "DampedOsc_PID";
    case sTune::NoOvershoot_PID:
        return "NoOvershoot_PID";
    case sTune::CohenCoon_PID:
        return "CohenCoon_PID";
    case sTune::Mixed_PID:
        return "Mixed_PID";
    case sTune::ZN_PI:
        return "ZN_PI";
    case sTune::DampedOsc_PI:
        return "DampedOsc_PI";
    case sTune::NoOvershoot_PI:
        return "NoOvershoot_PI";
    case sTune::CohenCoon_PI:
        return "CohenCoon_PI";
    case sTune::Mixed_PI:
        return "Mixed_PI";
    }
    return "Unknown";
}

/**
 * @brief Overload for numeric method identifiers from commands or telemetry.
 * @param method Numeric method index (0..9)
 * @return String literal representation
 */
[[nodiscard]] constexpr const char* getTuningMethodName(uint8_t method) {
    if (method <= static_cast<uint8_t>(sTune::Mixed_PI)) {
        return getTuningMethodName(static_cast<sTune::TuningMethod>(method));
    }
    return "Unknown";
}

/**
 * @class PidAutotuner
 * @brief Open-loop inflection point PID autotuner module.
 *
 * Automatically characterizes thermal transfer function parameters and computes
 * optimized Kp, Ki, Kd gains tailored for ceramic & quartz heating zones.
 *
 * CROSS-REFERENCED with:
 *   - pid::PIDController       (pid_controller.hpp) -> Gain Destination Target
 *   - config::Limits           (machine_config.hpp) -> MAX_TEMPERATURE Clamping
 *   - main::AppController      (app_controller.hpp) -> Control Task Loop Integration
 */
// ============================================================================
// sTune Inflection Point PID Autotuner
// ============================================================================

class PidAutotuner {
public:
    /**
     * @brief Construct a new PidAutotuner instance.
     */
    PidAutotuner() = default;

    /**
     * @brief Default destructor.
     */
    ~PidAutotuner() = default;

    /**
     * @brief Start an autotuning run for the given heater channel.
     * @param forTop true for Top heater, false for Bottom heater
     * @param targetTemp Target temperature (°C)
     * @param startTemp Current measured temperature at test start (°C)
     * @param method Selected PID calculation rule (defaults to sTune::Mixed_PID)
     */
    void begin(bool forTop, float targetTemp, float startTemp, sTune::TuningMethod method = sTune::Mixed_PID);

    /**
     * @brief Periodic step function called from the 200ms control task loop.
     * @param currentTemp Current measured temperature (°C)
     * @param[out] outputPower Output power to apply to heater (0.0f - 100.0f %)
     * @return true if autotuner is still running, false if finished or aborted
     */
    bool step(float currentTemp, float& outputPower);

    /**
     * @brief Abort autotuning immediately and safely set output to 0.
     */
    void abort();

    /**
     * @brief Apply computed tuning gains directly to a PIDController instance.
     * @param pid PIDController instance to update
     */
    void applyResults(PIDController& pid) const;

    /**
     * @brief Check whether autotuning is actively executing.
     * @return true if tuning in progress
     */
    [[nodiscard]] bool isRunning() const { return _running; }

    /**
     * @brief Check whether autotuning has completed successfully.
     * @return true if tuning finished with valid gains
     */
    [[nodiscard]] bool isFinished() const { return _finished; }

    /**
     * @brief Check whether active/last test is for the top heater.
     * @return true if Top heater, false if Bottom heater
     */
    [[nodiscard]] bool isTop() const { return _isTop; }

    /**
     * @brief Get selected tuning method rule for active/last test.
     * @return sTune::TuningMethod
     */
    [[nodiscard]] sTune::TuningMethod getTuningMethod() const { return _tuningMethod; }

    /**
     * @brief Get target temperature of active/last autotune test.
     * @return Target temperature in °C
     */
    [[nodiscard]] float getTargetTemp() const { return _targetTemp; }

    /**
     * @brief Get estimated autotuning progress percentage (0.0% to 100.0%).
     * @return Progress in percent
     */
    [[nodiscard]] float getProgressPercent() const;

    /**
     * @brief Retrieve computed PID gains from completed autotune test.
     * @param[out] kp Resulting proportional gain
     * @param[out] ki Resulting integral gain
     * @param[out] kd Resulting derivative gain
     */
    void getResults(float& kp, float& ki, float& kd) const {
        kp = _kp;
        ki = _ki;
        kd = _kd;
    }

    /**
     * @brief Set tuning results manually (used for deterministic unit testing and simulation).
     * @param isTop true for Top heater, false for Bottom heater
     * @param targetTemp Target temperature (°C)
     * @param kp Resulting proportional gain
     * @param ki Resulting integral gain
     * @param kd Resulting derivative gain
     */
    void setTestResults(bool isTopChannel, float targetTemp, float kp, float ki, float kd) {
        _isTop = isTopChannel;
        _targetTemp = targetTemp;
        _kp = kp;
        _ki = ki;
        _kd = kd;
        _finished = true;
        _running = false;
    }

private:
    bool _isTop = true;                                   ///< Active channel (true = Top, false = Bottom)
    float _targetTemp = 150.0f;                           ///< Target temperature setpoint for test (°C)
    float _eStopTemp = 280.0f;                            ///< Safety thermal shutdown threshold (°C)
    float _inputTemp = 0.0f;                              ///< Latest measured temperature input (°C)
    float _outputPower = 0.0f;                            ///< Active test heating power output (0.0% - 100.0%)
    bool _running = false;                                ///< Autotuning execution status flag
    bool _finished = false;                               ///< Autotuning success completion flag
    float _kp = 0.0f;                                     ///< Identified proportional gain
    float _ki = 0.0f;                                     ///< Identified integral gain
    float _kd = 0.0f;                                     ///< Identified derivative gain

    sTune::TuningMethod _tuningMethod = sTune::Mixed_PID; ///< Active tuning calculation method rule
    sTune _tuner{&_inputTemp, &_outputPower, sTune::Mixed_PID, sTune::directIP,
                 sTune::printSUMMARY};                    ///< Underlying sTune calculation engine
};

} // namespace pid
