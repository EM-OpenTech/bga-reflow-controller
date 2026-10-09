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
#include "esp_task_wdt.h"

static const char* TAG = "SafetyTask";

namespace app {

// ============================================================================
// Core 1 High-Priority Safety Watchdog Task Loop (20 Hz)
// ============================================================================

void safetyTask(void* pvParameters)
{
    auto* app = static_cast<AppController*>(pvParameters);
    ESP_LOGI(TAG, "Safety Task started on Core %d (Priority %d)",
             xPortGetCoreID(), (int)uxTaskPriorityGet(nullptr));

    // Allow hardware, power rails, and MAX31856 sensors a brief 1.5s warmup/conversion window at boot
    vTaskDelay(pdMS_TO_TICKS(1500));

    // Register with ESP-IDF Task Watchdog Timer
    esp_task_wdt_add(NULL);

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(50); // 20 Hz (50 ms)
    bool lastFaultState = false;

    while (true) {
        esp_task_wdt_reset();

        // In simulation mode or when safety watchdog is explicitly bypassed for testbenches, skip hardware checks
        if (app->getSettings().simulationMode || !app->getSettings().enableSafetyWatchdog) {
            vTaskDelayUntil(&lastWakeTime, frequency);
            continue;
        }

        // 1. Get atomic synchronized snapshot of readings & control states (Single-Writer mailbox)
        AppController::SensorSnapshot snap;
        app->getSensorSnapshot(snap);

        // 2. Perform safety checks using synchronized cycle data
        bool faultDetected = app->getSafety().check(snap.top, snap.bottom,
                                                   snap.topPower, snap.bottomPower,
                                                   snap.topSetpoint, snap.bottomSetpoint);

        // 3. Edge-triggered fault handling (only trigger FSM and log once on transition)
        if (faultDetected && !lastFaultState) {
            ESP_LOGE(TAG, "Safety fault triggered: %s", app->getSafety().getFaultString());
            // Latch emergency fault in FSM
            app->getFsm().triggerFault(snap.top.temperature, snap.bottom.temperature);
        } else if (!faultDetected && lastFaultState) {
            ESP_LOGI(TAG, "Safety fault condition cleared.");
        }
        lastFaultState = faultDetected;

        // Wait until next 50ms cycle
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

} // namespace app
