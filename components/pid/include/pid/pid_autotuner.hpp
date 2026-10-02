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
 * infrared heaters. Configured to run deterministically from the 100ms
 * FreeRTOS control task loop on Core 1.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include <cstdint>
#include "sTune.hpp"
#include "pid_controller.hpp"

namespace pid {

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
    PidAutotuner();

    /**
     * @brief Default destructor.
     */
    ~PidAutotuner() = default;

    /**
     * @brief Start an autotuning run for the given heater channel.
     * @param isTop true for Top heater, false for Bottom heater
     * @param targetTemp Target temperature (°C)
     * @param startTemp Current measured temperature at test start (°C)
     */
    void begin(bool isTop, float targetTemp, float startTemp);

    /**
     * @brief Periodic step function called from the 100ms control task loop.
     * @param currentTemp Current measured temperature (°C)
     * @param[out] outputPower Output power to apply to heater (0.0f - 100.0f %)
     * @return true if autotuner is still running, false if finished or aborted
     */
    bool step(float currentTemp, float &outputPower);

    /**
     * @brief Abort autotuning immediately and safely set output to 0.
     */
    void abort();

    /**
     * @brief Apply computed tuning gains directly to a PIDController instance.
     * @param pid PIDController instance to update
     */
    void applyResults(PIDController &pid);

    /**
     * @brief Check whether autotuning is actively executing.
     * @return true if tuning in progress
     */
    bool isRunning() const { return _running; }

    /**
     * @brief Check whether autotuning has completed successfully.
     * @return true if tuning finished with valid gains
     */
    bool isFinished() const { return _finished; }

    /**
     * @brief Check whether active/last test is for the top heater.
     * @return true if Top heater, false if Bottom heater
     */
    bool isTop() const { return _isTop; }

    /**
     * @brief Get target temperature of active/last autotune test.
     * @return Target temperature in °C
     */
    float getTargetTemp() const { return _targetTemp; }

    /**
     * @brief Get estimated autotuning progress percentage (0.0% to 100.0%).
     * @return Progress in percent
     */
    float getProgressPercent() const;

    /**
     * @brief Retrieve computed PID gains from completed autotune test.
     * @param[out] kp Resulting proportional gain
     * @param[out] ki Resulting integral gain
     * @param[out] kd Resulting derivative gain
     */
    void getResults(float &kp, float &ki, float &kd) const {
        kp = _kp;
        ki = _ki;
        kd = _kd;
    }

private:
    bool     _isTop         = true;   ///< Active channel (true = Top, false = Bottom)
    float    _targetTemp    = 150.0f; ///< Target temperature setpoint for test (°C)
    float    _eStopTemp     = 280.0f; ///< Safety thermal shutdown threshold (°C)
    float    _inputTemp     = 0.0f;   ///< Latest measured temperature input (°C)
    float    _outputPower   = 0.0f;   ///< Active test heating power output (0.0% - 100.0%)
    bool     _running       = false;  ///< Autotuning execution status flag
    bool     _finished      = false;  ///< Autotuning success completion flag
    float    _kp            = 0.0f;   ///< Identified proportional gain
    float    _ki            = 0.0f;   ///< Identified integral gain
    float    _kd            = 0.0f;   ///< Identified derivative gain

    sTune    _tuner;                  ///< Underlying sTune calculation engine
};

} // namespace pid
