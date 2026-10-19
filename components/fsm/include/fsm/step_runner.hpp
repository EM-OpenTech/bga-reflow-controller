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
 * @file step_runner.hpp
 * @brief Independent Profile Step Trajectory & Settle-Gate Controller.
 *
 * Manages the execution lifecycle of profile steps for a single heater channel:
 * RAMPING (linear setpoint interpolation) -> SETTLING (temperature stability gate)
 * -> HOLDING (hold duration countdown) -> advance to next step or complete.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "config/machine_config.hpp"
#include <cstddef>
#include <cstdint>

namespace fsm {

/**
 * @brief Execution phase of a single profile step.
 */
enum class StepPhase : uint8_t {
    IDLE = 0, ///< Inactive / awaiting profile start
    RAMPING,  ///< Actively ramping setpoint towards target temperature
    SETTLING, ///< In Settle-Gate stability window awaiting steady-state
    HOLDING,  ///< Holding target temperature for specified duration
    FINISHED  ///< All profile steps completed
};

/**
 * @brief Event notification emitted by StepRunner during update().
 */
enum class StepEvent : uint8_t {
    NONE = 0,       ///< No phase transition occurred in this tick
    ENTERED_SETTLE, ///< Ramp finished; entered settle-gate window
    ENTERED_HOLD,   ///< Settle criteria met; entered hold phase
    STEP_ADVANCED,  ///< Hold finished or step skipped; advanced to next step
    FINISHED        ///< Final step completed; all profile steps finished
};

/**
 * @class StepRunner
 * @brief Independent trajectory runner and settle-gate manager for one heater channel.
 */
class StepRunner {
public:
    /**
     * @brief Construct StepRunner.
     * @param isTop true for Top heater channel, false for Bottom heater channel
     * @param settings Reference to loaded machine settings (tolerances, settle time)
     */
    StepRunner(bool isTop, const config::MachineSettings& settings);

    /**
     * @brief Start executing a profile step sequence.
     * @param steps Pointer to array of profile steps
     * @param stepCount Total number of steps in sequence
     * @param startTemp Starting temperature for initial ramp (°C)
     */
    void start(const config::ProfileStep* steps, size_t stepCount, float startTemp);

    /**
     * @brief Reset runner state to idle and clear all timers.
     */
    void reset();

    /**
     * @brief Periodic update tick (typically called at 5 Hz / 200 ms).
     *
     * Advances ramp interpolation, evaluates settle-gate stability window,
     * and counts down hold duration.
     *
     * @param actualTemp Current measured thermocouple temperature (°C)
     * @param dtMs Elapsed time since last tick in milliseconds
     * @return StepEvent indicating if a significant transition occurred
     */
    StepEvent update(float actualTemp, uint32_t dtMs);

    /**
     * @brief Skip current step and immediately advance to the next step.
     * @return StepEvent::STEP_ADVANCED or StepEvent::FINISHED
     */
    StepEvent skipStep();

    // ========================================================================
    // Status queries
    // ========================================================================

    [[nodiscard]] StepPhase getPhase() const { return _phase; }
    [[nodiscard]] bool isFinished() const { return _phase == StepPhase::FINISHED; }
    [[nodiscard]] bool isRamping() const { return _phase == StepPhase::RAMPING; }
    [[nodiscard]] bool isHolding() const { return _phase == StepPhase::HOLDING; }

    /**
     * @brief Check if channel is actively settling and within approach threshold (target - 5°C).
     * @param actualTemp Current measured temperature (°C)
     * @return true if settling within display threshold
     */
    [[nodiscard]] bool isSettling(float actualTemp) const;

    /**
     * @brief Get remaining settle-gate countdown in seconds (rounded up).
     * @param actualTemp Current measured temperature (°C)
     * @return Settle countdown in seconds
     */
    [[nodiscard]] uint32_t getSettleRemainSec(float actualTemp) const;

    /**
     * @brief Get remaining hold time in seconds.
     * @return Remaining hold duration in seconds
     */
    [[nodiscard]] uint32_t getHoldRemainSec() const;

    /**
     * @brief Get current interpolated setpoint (°C).
     * @return Setpoint in °C
     */
    [[nodiscard]] float getCurrentSetpoint() const { return _currentSet; }

    /**
     * @brief Force an override setpoint value (e.g. For preheat hold temperature).
     * @param setpoint Target temperature (°C)
     */
    void setCurrentSetpoint(float setpoint) { _currentSet = setpoint; }

    /**
     * @brief Get target temperature of current step (°C).
     * @return Target temperature in °C (0.0f if finished/idle)
     */
    [[nodiscard]] float getTargetTemp() const;

    /**
     * @brief Get current step index (0-based).
     * @return Step index
     */
    [[nodiscard]] size_t getStepIndex() const { return _stepIndex; }

    /**
     * @brief Check whether step marker has already been emitted for current step.
     */
    [[nodiscard]] bool isMarkerSent() const { return _stepMarkerSent; }

    /**
     * @brief Mark step marker as sent for current step.
     */
    void markMarkerSent() { _stepMarkerSent = true; }

private:
    bool _isTop = false;
    const config::MachineSettings& _settings;

    const config::ProfileStep* _steps = nullptr;
    size_t _stepCount = 0;
    size_t _stepIndex = 0;

    StepPhase _phase = StepPhase::IDLE;
    float _currentSet = 0.0f;
    float _stepStartSet = 0.0f;
    uint32_t _settleAccumMs = 0;
    uint32_t _holdRemainMs = 0;
    bool _stepMarkerSent = false;

    StepEvent processRamping(uint32_t dtMs);
    StepEvent processSettling(float actualTemp, uint32_t dtMs);
    StepEvent processHolding(uint32_t dtMs);
    StepEvent advanceToNextStep();
};

} // namespace fsm
