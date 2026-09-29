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
 * @file safety_task.cpp
 * @brief Implementation of Core 1 20 Hz Safety Watchdog Task.
 *
 * Evaluates over-temperature, IC hardware errors, sensor wire breaks, stuck SSRs,
 * and heater failures, immediately triggering hardware inhibit on violations.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "tasks/safety_task.hpp"
#include "app_controller.hpp"
#include "esp_log.h"

static const char* TAG = "SafetyTask";

namespace app {

void safetyTask(void* pvParameters)
{
    auto* app = static_cast<AppController*>(pvParameters);
    ESP_LOGI(TAG, "Safety Task started on Core %d (Priority %d, 20 Hz)",
             xPortGetCoreID(), (int)uxTaskPriorityGet(nullptr));

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(50); // 20 Hz (50 ms)

    while (true) {
        // In simulation mode, skip hardware safety watchdog to allow testing without connected thermocouples
        if (app->getSettings().simulationMode) {
            vTaskDelayUntil(&lastWakeTime, frequency);
            continue;
        }

        // 1. Get latest readings and power levels
        const auto& topReading = app->getTopSensor().getLatest();
        const auto& botReading = app->getBottomSensor().getLatest();
        float topPower = app->getTopPid().getOutput();
        float botPower = app->getBottomPid().getOutput();
        float topSet = app->getFsm().getTopSetpoint();
        float botSet = app->getFsm().getBottomSetpoint();

        // 2. Perform safety checks
        bool faultDetected = app->getSafety().check(topReading, botReading, topPower, botPower, topSet, botSet);

        if (faultDetected) {
            ESP_LOGE(TAG, "Safety fault triggered: %s", app->getSafety().getFaultString());
            // Latch emergency fault in FSM
            app->getFsm().triggerFault(topReading.temperature, botReading.temperature);
        }

        // Wait until next 50ms cycle
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

} // namespace app
