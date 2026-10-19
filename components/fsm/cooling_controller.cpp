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
 * @file cooling_controller.cpp
 * @brief Independent Cooling Phase, Fan Timing & Lamp Override Controller Implementation.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "fsm/cooling_controller.hpp"
#include "esp_log.h"

namespace {
constexpr const char* TAG = "COOLING_CTRL";
} // namespace

namespace fsm {

CoolingController::CoolingController(output::OutputManager& outputs, const config::MachineSettings& settings)
    : _outputs(outputs), _settings(settings) {}

void CoolingController::startCooling(bool immediate) {
    _coolingElapsedMs = 0;
    _fanRunMs = 0;

    // Immediate fan activation if requested or delay is 0, otherwise wait for delay timer
    _fanAuto = immediate || (_settings.fanCoolingDelayS == 0);
    applyOutputs();

    ESP_LOGI(TAG, "Entering COOLING (fanDelay=%lus, fanMinDuration=%lus, safeTemp=%.1f°C, immediate=%s)",
             static_cast<unsigned long>(_settings.fanCoolingDelayS),
             static_cast<unsigned long>(_settings.fanCoolingDurationS), static_cast<double>(_settings.coolingSafeTemp),
             immediate ? "true" : "false");
}

bool CoolingController::update(float topTemp, float bottomTemp, uint32_t dtMs) {
    _coolingElapsedMs += dtMs;

    // Evaluate turn-on delay timer
    if (!_fanAuto && _coolingElapsedMs >= _settings.fanCoolingDelayS * 1000UL) {
        _fanAuto = true;
        applyOutputs();
        ESP_LOGI(TAG, "COOLING fan delay expired -> fan ON");
    }

    if (_fanAuto) {
        _fanRunMs += dtMs;
    }

    // Completion criteria:
    // 1. Duration check: in passive cooling (fanCoolingDurationS == 0), no fan run time is required.
    // 2. Dual-channel temperature safe check: BOTH top and bottom heaters must be <= coolingSafeTemp.
    bool durationOk = (_settings.fanCoolingDurationS == 0) || (_fanRunMs >= _settings.fanCoolingDurationS * 1000UL);
    bool tempOk = (bottomTemp <= _settings.coolingSafeTemp && topTemp <= _settings.coolingSafeTemp);

    if (!durationOk || !tempOk) {
        return false;
    }

    _fanAuto = false;
    applyOutputs();
    ESP_LOGI(TAG, "COOLING complete (both heaters <= %.1f°C) -> ready for DONE",
             static_cast<double>(_settings.coolingSafeTemp));
    return true;
}

void CoolingController::stopCooling() {
    _fanAuto = false;
    applyOutputs();
}

void CoolingController::handleFault(float topTemp, float bottomTemp) {
    // Guard: Only evaluate fan shut-off if fan was auto-started during fault
    if (!_fanAuto) {
        return;
    }

    if (topTemp <= _settings.coolingSafeTemp && bottomTemp <= _settings.coolingSafeTemp) {
        _fanAuto = false;
        applyOutputs();
        ESP_LOGI(TAG, "FAULT temperatures safe (<= %.1f°C) -> fan OFF", static_cast<double>(_settings.coolingSafeTemp));
    }
}

void CoolingController::setFanOverride(bool on) {
    _fanOverride = on;
    applyOutputs();
}

void CoolingController::setLampOverride(bool on) {
    _lampOverride = on;
    applyOutputs();
}

void CoolingController::applyOutputs() {
    _outputs.setFan(isFanEffective());
    _outputs.setLamp(isLampEffective());
}

} // namespace fsm
