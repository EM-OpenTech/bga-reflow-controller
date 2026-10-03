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
 * @file burstfire_task.cpp
 * @brief Implementation of Core 1 100 Hz SSR Burst-Fire Modulation Task.
 *
 * Advances window time accumulators and modulates GPIO SSR gates synchronously.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "tasks/burstfire_task.hpp"
#include "app_controller.hpp"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char* TAG = "BurstFireTask";

namespace app {

// ============================================================================
// Core 1 High-Frequency Burst-Fire SSR PWM Task Loop (100 Hz)
// ============================================================================

void burstfireTask(void* pvParameters)
{
    auto* app = static_cast<AppController*>(pvParameters);
    ESP_LOGI(TAG, "BurstFire Task started on Core %d (Priority %d, 100 Hz / 10ms)",
             xPortGetCoreID(), (int)uxTaskPriorityGet(nullptr));

    // Register with ESP-IDF Task Watchdog Timer
    esp_task_wdt_add(NULL);

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(10); // 100 Hz (10 ms)

    while (true) {
        esp_task_wdt_reset();

        // 1. Advance Burst-Fire PWM Windows
        app->getTopBurst().update();
        app->getBottomBurst().update();

        // 2. Drive Solid State Relay Hardware Outputs
        bool topSsrOn = app->getTopBurst().getState();
        bool botSsrOn = app->getBottomBurst().getState();

        app->getOutputs().setSsrTop(topSsrOn);
        app->getOutputs().setSsrBottom(botSsrOn);

        // 3. Wait for Next 10 ms Tick with Zero Cumulative Drift
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

} // namespace app
