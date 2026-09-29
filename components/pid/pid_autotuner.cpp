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

namespace pid {

static const char *TAG = "PidAutotuner";

PidAutotuner::PidAutotuner()
    : _tuner(&_inputTemp, &_outputPower, sTune::NoOvershoot_PID, sTune::directIP, sTune::printSUMMARY)
{
}

void PidAutotuner::begin(bool isTop, float targetTemp, float startTemp)
{
    _isTop       = isTop;
    _targetTemp  = targetTemp;
    _inputTemp   = startTemp;
    _outputPower = 0.0f;
    _running     = true;
    _finished    = false;
    _kp          = 0.0f;
    _ki          = 0.0f;
    _kd          = 0.0f;

    _tuner.SetTuningMethod(sTune::NoOvershoot_PID);
    _tuner.SetControllerAction(sTune::directIP);
    _tuner.SetSerialMode(sTune::printSUMMARY);

    // Emergency stop threshold: 40°C above target temperature
    _eStopTemp = targetTemp + 40.0f;
    if (_eStopTemp > config::Limits::MAX_TEMPERATURE) {
        _eStopTemp = config::Limits::MAX_TEMPERATURE;
    }

    if (_isTop) {
        // Top Heater (Quicker): 250s total test time, 15s settle, 250 samples -> 1000ms sample period
        _tuner.Configure(300.0f, 100.0f, 0.0f, 100.0f, 250, 15, 250);
        ESP_LOGI(TAG, "Autotune started for TOP heater at target %.1f°C (eStop=%.1f°C, samplePeriod=1000ms)",
                 targetTemp, _eStopTemp);
    } else {
        // Bottom Heater (Slower): 400s total test time, 15s settle, 400 samples -> 1000ms sample period
        _tuner.Configure(300.0f, 100.0f, 0.0f, 100.0f, 400, 15, 400);
        ESP_LOGI(TAG, "Autotune started for BOTTOM heater at target %.1f°C (eStop=%.1f°C, samplePeriod=1000ms)",
                 targetTemp, _eStopTemp);
    }
    // NOTE: SetEmergencyStop() MUST be called AFTER Configure(), because Configure()
    // internally resets eStop = inputSpan (300°C), which would overwrite any earlier call.
    _tuner.SetEmergencyStop(_eStopTemp);
}

bool PidAutotuner::step(float currentTemp, float &outputPower)
{
    if (!_running) {
        outputPower = 0.0f;
        return false;
    }

    // Immediate emergency stop check
    if (currentTemp > _eStopTemp) {
        ESP_LOGE(TAG, "Autotune EMERGENCY STOP: Temp %.1f°C exceeded threshold %.1f°C",
                 currentTemp, _eStopTemp);
        abort();
        outputPower = 0.0f;
        return false;
    }

    _inputTemp = currentTemp;
    uint8_t status = _tuner.Run();
    outputPower = _outputPower;

    // Check if tuning calculation has completed
    if (status == sTune::tunings || _tuner.GetSampleCount() >= _tuner.GetTotalSamples()) {
        _tuner.GetAutoTunings(&_kp, &_ki, &_kd);
        _running = false;
        _finished = true;
        _outputPower = 0.0f;
        outputPower = 0.0f;

        ESP_LOGI(TAG, "==================================================");
        ESP_LOGI(TAG, "AUTOTUNE COMPLETE for %s heater at %.1f°C",
                 _isTop ? "TOP" : "BOTTOM", _targetTemp);
        ESP_LOGI(TAG, "Calculated Gains: Kp=%.3f | Ki=%.4f | Kd=%.3f", _kp, _ki, _kd);
        ESP_LOGI(TAG, "==================================================");
        return false;
    }

    return true;
}

void PidAutotuner::abort()
{
    if (_running) {
        ESP_LOGW(TAG, "Autotune ABORTED for %s heater", _isTop ? "TOP" : "BOTTOM");
    }
    _running = false;
    _finished = false;
    _outputPower = 0.0f;
    _tuner.Reset();
}

void PidAutotuner::applyResults(PIDController &pid)
{
    if (_finished && _kp > 0.0f) {
        pid.setTunings(_kp, _ki, _kd);
        ESP_LOGI(TAG, "Applied autotune gains to PIDController: Kp=%.3f, Ki=%.4f, Kd=%.3f",
                 _kp, _ki, _kd);
    }
}

float PidAutotuner::getProgressPercent() const
{
    if (_finished) return 100.0f;
    if (!_running) return 0.0f;
    return _tuner.GetProgress();
}

} // namespace pid
