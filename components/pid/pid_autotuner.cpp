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
 * @file pid_autotuner.cpp
 * @brief Implementation of sTune PID Autotuner wrapper.
 *
 * Configured for open-loop inflection point autotuning of infrared
 * ceramic and quartz heaters with safety guards and emergency limits.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "pid/pid_autotuner.hpp"
#include "config/machine_config.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cmath>

namespace pid {

namespace {

constexpr const char* TAG = "PidAutotuner";

struct ChannelTuneProfile {
    float inputSpan = 0.0f;
    float outputSpan = 0.0f;
    float outputStart = 0.0f;
    float outputStep = 0.0f;
    uint32_t testTimeSec = 0;
    uint32_t settleTimeSec = 0;
    uint16_t samples = 0;
};

// Top Heater (Quicker): 250s total test time, 15s settle, 250 samples -> 1000ms sample period
constexpr ChannelTuneProfile TOP_TUNE_PROFILE = {.inputSpan = 300.0f,
                                                 .outputSpan = 100.0f,
                                                 .outputStart = 0.0f,
                                                 .outputStep = 30.0f,
                                                 .testTimeSec = 250,
                                                 .settleTimeSec = 15,
                                                 .samples = 250};

// Bottom Heater (Slower): 400s total test time, 15s settle, 400 samples -> 1000ms sample period
constexpr ChannelTuneProfile BOTTOM_TUNE_PROFILE = {.inputSpan = 300.0f,
                                                    .outputSpan = 100.0f,
                                                    .outputStart = 0.0f,
                                                    .outputStep = 40.0f,
                                                    .testTimeSec = 400,
                                                    .settleTimeSec = 15,
                                                    .samples = 400};

/**
 * @brief Validates identified PID gains against domain boundaries and IEEE-754 NaN checks.
 */
[[nodiscard]] bool areGainsValid(float kp, float ki, float kd) {
    if (!std::isfinite(kp) || !std::isfinite(ki) || !std::isfinite(kd)) {
        return false;
    }
    if (kp <= config::Limits::MIN_PID_KP || kp > config::Limits::MAX_PID_KP) {
        return false;
    }
    if (ki < config::Limits::MIN_PID_KI || ki > config::Limits::MAX_PID_KI) {
        return false;
    }
    if (kd < config::Limits::MIN_PID_KD || kd > config::Limits::MAX_PID_KD) {
        return false;
    }
    return true;
}

} // anonymous namespace

// ============================================================================
// Lifecycle & sTune Engine Configuration
// ============================================================================

void PidAutotuner::begin(bool forTop, float targetTemp, float startTemp, sTune::TuningMethod method) {
    if (!std::isfinite(targetTemp) || !std::isfinite(startTemp)) {
        ESP_LOGE(TAG, "Cannot start autotuner: targetTemp or startTemp is NaN/Inf!");
        abort();
        return;
    }

    _isTop = forTop;
    _targetTemp = targetTemp;
    _inputTemp = startTemp;
    _tuningMethod = method;
    _outputPower = 0.0f;
    _running = true;
    _finished = false;
    _kp = 0.0f;
    _ki = 0.0f;
    _kd = 0.0f;

    _tuner.SetTuningMethod(_tuningMethod);
    _tuner.SetControllerAction(sTune::directIP);
    _tuner.SetSerialMode(sTune::printSUMMARY);

    // Emergency stop threshold: 40°C above target temperature, clamped to absolute maximum
    _eStopTemp = std::min(targetTemp + 40.0f, config::Limits::MAX_TEMPERATURE);

    const auto& profile = _isTop ? TOP_TUNE_PROFILE : BOTTOM_TUNE_PROFILE;
    _tuner.Configure(profile.inputSpan, profile.outputSpan, profile.outputStart, profile.outputStep,
                     profile.testTimeSec, profile.settleTimeSec, profile.samples);

    ESP_LOGI(TAG, "Autotune started for %s heater at target %.1f°C (method=%s [%u], eStop=%.1f°C, samplePeriod=1000ms)",
             _isTop ? "TOP" : "BOTTOM", static_cast<double>(targetTemp), getTuningMethodName(_tuningMethod),
             static_cast<unsigned>(_tuningMethod), static_cast<double>(_eStopTemp));

    // NOTE: SetEmergencyStop() MUST be called AFTER Configure(), because Configure()
    // internally resets eStop = inputSpan (300°C), which would overwrite any earlier call.
    _tuner.SetEmergencyStop(_eStopTemp);
}

// ============================================================================
// Periodic Autotune Step & Inflection Point Evaluation
// ============================================================================

bool PidAutotuner::step(float currentTemp, float& outputPower) {
    if (!_running) {
        outputPower = 0.0f;
        return false;
    }

    // Immediate emergency stop check (guards against sensor NaN/Inf faults and overtemperature)
    if (!std::isfinite(currentTemp) || currentTemp > _eStopTemp) {
        ESP_LOGE(TAG, "Autotune EMERGENCY STOP: Temp %s threshold %.1f°C",
                 (!std::isfinite(currentTemp)) ? "is NaN/Inf (sensor fault), aborting against" : "exceeded",
                 static_cast<double>(_eStopTemp));
        abort();
        outputPower = 0.0f;
        return false;
    }

    _inputTemp = currentTemp;
    uint8_t status = _tuner.Run();
    outputPower = _outputPower;

    if (status != sTune::tunings) {
        return true;
    }

    // Check if tuning calculation has completed
    _tuner.GetAutoTunings(&_kp, &_ki, &_kd);
    _running = false;
    _finished = true;
    _outputPower = 0.0f;
    outputPower = 0.0f;

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "AUTOTUNE COMPLETE for %s heater at %.1f°C (method=%s)", _isTop ? "TOP" : "BOTTOM",
             static_cast<double>(_targetTemp), getTuningMethodName(_tuningMethod));
    ESP_LOGI(TAG, "Calculated Gains: Kp=%.3f | Ki=%.4f | Kd=%.3f", static_cast<double>(_kp), static_cast<double>(_ki),
             static_cast<double>(_kd));
    ESP_LOGI(TAG, "==================================================");

    return false;
}

// ============================================================================
// Abort & Result Transfer
// ============================================================================

void PidAutotuner::abort() {
    if (_running) {
        ESP_LOGW(TAG, "Autotune ABORTED for %s heater", _isTop ? "TOP" : "BOTTOM");
    }
    _running = false;
    _finished = false;
    _outputPower = 0.0f;
    _tuner.Reset();
}

void PidAutotuner::applyResults(PIDController& pid) const {
    if (!_finished) {
        return;
    }

    if (!areGainsValid(_kp, _ki, _kd)) {
        ESP_LOGW(TAG, "Autotune calculated gains invalid or out of limits: Kp=%.3f, Ki=%.4f, Kd=%.3f -> not applied",
                 static_cast<double>(_kp), static_cast<double>(_ki), static_cast<double>(_kd));
        return;
    }

    pid.setTunings(_kp, _ki, _kd);
    ESP_LOGI(TAG, "Applied autotune gains to PIDController: Kp=%.3f, Ki=%.4f, Kd=%.3f", static_cast<double>(_kp),
             static_cast<double>(_ki), static_cast<double>(_kd));
}

float PidAutotuner::getProgressPercent() const {
    if (_finished) {
        return 100.0f;
    }
    if (!_running) {
        return 0.0f;
    }
    return _tuner.GetProgress();
}

} // namespace pid
