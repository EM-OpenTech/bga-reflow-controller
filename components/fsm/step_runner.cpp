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
 * @file step_runner.cpp
 * @brief Independent Profile Step Trajectory & Settle-Gate Controller Implementation.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "fsm/step_runner.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr const char* TAG = "STEP_RUNNER";
} // namespace

namespace fsm {

StepRunner::StepRunner(bool isTop, const config::MachineSettings& settings) : _isTop(isTop), _settings(settings) {}

void StepRunner::start(const config::ProfileStep* steps, size_t stepCount, float startTemp) {
    _steps = steps;
    _stepCount = stepCount;
    _stepIndex = 0;
    _settleAccumMs = 0;
    _holdRemainMs = 0;
    _stepMarkerSent = false;

    if (_steps == nullptr || _stepCount == 0) {
        _phase = StepPhase::FINISHED;
        _currentSet = 0.0f;
        _stepStartSet = 0.0f;
        return;
    }

    _stepStartSet = startTemp;
    _currentSet = startTemp;
    _phase = StepPhase::RAMPING;

    ESP_LOGI(TAG, "%s channel started profile with %zu steps (start=%.1f°C)", _isTop ? "Top" : "Bottom", _stepCount,
             static_cast<double>(startTemp));
}

void StepRunner::reset() {
    _steps = nullptr;
    _stepCount = 0;
    _stepIndex = 0;
    _phase = StepPhase::IDLE;
    _currentSet = 0.0f;
    _stepStartSet = 0.0f;
    _settleAccumMs = 0;
    _holdRemainMs = 0;
    _stepMarkerSent = false;
}

StepEvent StepRunner::update(float actualTemp, uint32_t dtMs) {
    if (_phase == StepPhase::FINISHED || _phase == StepPhase::IDLE) {
        return StepEvent::NONE;
    }

    // Safety guard against invalid sensor readings
    if (std::isnan(actualTemp) || std::isinf(actualTemp)) {
        return StepEvent::NONE;
    }

    switch (_phase) {
    case StepPhase::RAMPING:
        return processRamping(dtMs);

    case StepPhase::SETTLING:
        return processSettling(actualTemp, dtMs);

    case StepPhase::HOLDING:
        return processHolding(dtMs);

    default:
        return StepEvent::NONE;
    }
}

StepEvent StepRunner::processRamping(uint32_t dtMs) {
    if (_steps == nullptr || _stepIndex >= _stepCount) {
        _phase = StepPhase::FINISHED;
        return StepEvent::FINISHED;
    }

    const config::ProfileStep& step = _steps[_stepIndex];

    // Handle instant jump (ramp rate <= 0 or downward setpoint transition)
    // Downward transitions jump directly to lower target to avoid unnecessary heater firing during cooling
    if (step.ramp <= 0.01f || step.temp < _stepStartSet) {
        _currentSet = step.temp;
        _phase = StepPhase::SETTLING;
        _settleAccumMs = 0;
        ESP_LOGD(TAG, "%s step %zu instant setpoint jump -> %.1f°C (%s)", _isTop ? "Top" : "Bot", _stepIndex,
                 static_cast<double>(step.temp), (step.temp < _stepStartSet) ? "downward step" : "instant ramp");
        return StepEvent::ENTERED_SETTLE;
    }

    // Upward ramp interpolation
    float rampDeltaPerMs = step.ramp / 1000.0f; // °C/ms
    _currentSet += rampDeltaPerMs * static_cast<float>(dtMs);

    if (_currentSet >= step.temp) {
        _currentSet = step.temp;
        _phase = StepPhase::SETTLING;
        _settleAccumMs = 0;
        ESP_LOGD(TAG, "%s step %zu upward ramp reached target %.1f°C -> settle", _isTop ? "Top" : "Bot", _stepIndex,
                 static_cast<double>(step.temp));
        return StepEvent::ENTERED_SETTLE;
    }

    return StepEvent::NONE;
}

StepEvent StepRunner::processSettling(float actualTemp, uint32_t dtMs) {
    if (_steps == nullptr || _stepIndex >= _stepCount) {
        _phase = StepPhase::FINISHED;
        return StepEvent::FINISHED;
    }

    const config::ProfileStep& step = _steps[_stepIndex];
    float lowTol = step.temp - _settings.holdLowTolerance;
    float highTol = step.temp + _settings.holdHighTolerance;

    // Guard: Temperature outside tolerance window -> reset stability timer and return
    if (actualTemp < lowTol || actualTemp > highTol) {
        if (_settleAccumMs > 0) {
            ESP_LOGD(TAG, "%s step %zu settle reset (T=%.1f outside [%.1f, %.1f])", _isTop ? "Top" : "Bot", _stepIndex,
                     static_cast<double>(actualTemp), static_cast<double>(lowTol), static_cast<double>(highTol));
        }
        _settleAccumMs = 0;
        return StepEvent::NONE;
    }

    // Accumulate settle stability time
    _settleAccumMs += dtMs;
    uint32_t requiredSettleMs = _settings.settleTimeS * 1000UL;
    if (_settleAccumMs < requiredSettleMs) {
        return StepEvent::NONE;
    }

    // Settle gate passed: deduct settle time from configured hold duration
    uint32_t settleS = _settings.settleTimeS;
    uint32_t holdS = (step.time > settleS) ? (step.time - settleS) : 0UL;
    _holdRemainMs = holdS * 1000UL;
    _phase = StepPhase::HOLDING;

    ESP_LOGI(TAG, "%s step %zu settled at %.1f°C, hold=%lus", _isTop ? "Top" : "Bot", _stepIndex,
             static_cast<double>(step.temp), static_cast<unsigned long>(holdS));

    return StepEvent::ENTERED_HOLD;
}

StepEvent StepRunner::processHolding(uint32_t dtMs) {
    if (_holdRemainMs <= dtMs) {
        _holdRemainMs = 0;
        return advanceToNextStep();
    }

    _holdRemainMs -= dtMs;
    return StepEvent::NONE;
}

StepEvent StepRunner::advanceToNextStep() {
    _stepIndex++;
    if (_stepIndex >= _stepCount) {
        _phase = StepPhase::FINISHED;
        ESP_LOGI(TAG, "%s all %zu steps complete -> FINISHED", _isTop ? "Top" : "Bot", _stepCount);
        return StepEvent::FINISHED;
    }

    _stepStartSet = _currentSet;
    _phase = StepPhase::RAMPING;
    _settleAccumMs = 0;
    _stepMarkerSent = false;

    ESP_LOGI(TAG, "%s advanced to step %zu/%zu (target=%.1f°C)", _isTop ? "Top" : "Bot", _stepIndex + 1, _stepCount,
             static_cast<double>(_steps[_stepIndex].temp));

    return StepEvent::STEP_ADVANCED;
}

StepEvent StepRunner::skipStep() {
    if (_phase == StepPhase::FINISHED || _phase == StepPhase::IDLE) {
        return StepEvent::NONE;
    }
    ESP_LOGI(TAG, "%s step %zu skipped by user", _isTop ? "Top" : "Bot", _stepIndex);
    return advanceToNextStep();
}

bool StepRunner::isSettling(float actualTemp) const {
    if (_phase != StepPhase::SETTLING || _steps == nullptr || _stepIndex >= _stepCount) {
        return false;
    }
    const float target = _steps[_stepIndex].temp;
    const float maxTolerance = std::max({5.0f, _settings.holdLowTolerance, _settings.holdHighTolerance});
    return (std::abs(actualTemp - target) <= maxTolerance);
}

uint32_t StepRunner::getSettleRemainSec(float actualTemp) const {
    if (!isSettling(actualTemp)) {
        return 0;
    }
    uint32_t targetMs = _settings.settleTimeS * 1000UL;
    uint32_t remainMs = (targetMs > _settleAccumMs) ? (targetMs - _settleAccumMs) : 0;
    return (remainMs + 999UL) / 1000UL;
}

uint32_t StepRunner::getHoldRemainSec() const {
    if (_phase != StepPhase::HOLDING) {
        return 0;
    }
    return _holdRemainMs / 1000UL;
}

float StepRunner::getTargetTemp() const {
    if (_steps == nullptr || _stepIndex >= _stepCount) {
        return 0.0f;
    }
    return _steps[_stepIndex].temp;
}

} // namespace fsm
