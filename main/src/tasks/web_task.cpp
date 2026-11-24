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
 * @file web_task.cpp
 * @brief Implementation of Core 0 2 Hz Live WebSocket Telemetry Streaming Task.
 *
 * Extracts periodic atomic snapshots from SystemContext and broadcasts WebSocket frames.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "tasks/web_task.hpp"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "system_context.hpp"
#include "web/api_context.hpp"
#include "web/web_server.hpp"

namespace app {

namespace {

constexpr const char* TAG = "WebTask";

web::TelemetryData buildTelemetryData(const SystemContextData& snapshot) {
    web::TelemetryData t;
    t.stateEnum = static_cast<uint8_t>(snapshot.state);
    t.stateStr = snapshot.stateStr;
    t.preheatDone = snapshot.preheatDone;
    t.topTemp = snapshot.topTemp;
    t.bottomTemp = snapshot.bottomTemp;
    t.topSensorOk = snapshot.topSensorOk;
    t.bottomSensorOk = snapshot.bottomSensorOk;
    t.topSet = snapshot.topSetpoint;
    t.bottomSet = snapshot.bottomSetpoint;
    t.topPower = snapshot.topPower;
    t.bottomPower = snapshot.bottomPower;
    t.elapsedSec = snapshot.elapsedSec;
    t.talSec = snapshot.talSec;
    t.fan = snapshot.fanActive;
    t.lamp = snapshot.lampActive;
    t.topStep = snapshot.topStep;
    t.bottomStep = snapshot.bottomStep;
    t.topSettling = snapshot.topSettling;
    t.bottomSettling = snapshot.bottomSettling;
    t.topHolding = snapshot.topHolding;
    t.bottomHolding = snapshot.bottomHolding;
    t.topSettleRemain = snapshot.topSettleRemain;
    t.bottomSettleRemain = snapshot.bottomSettleRemain;
    t.topHoldRemain = snapshot.topHoldRemain;
    t.bottomHoldRemain = snapshot.bottomHoldRemain;
    t.profileFile = snapshot.activeProfileFile;
    t.stepMarkers = snapshot.stepMarkers;
    t.topPidKp = snapshot.topPidKp;
    t.topPidKi = snapshot.topPidKi;
    t.topPidKd = snapshot.topPidKd;
    t.bottomPidKp = snapshot.bottomPidKp;
    t.bottomPidKi = snapshot.bottomPidKi;
    t.bottomPidKd = snapshot.bottomPidKd;
    t.autotuneActive = snapshot.autotuneActive;
    t.autotuneFinished = snapshot.autotuneFinished;
    t.autotuneIsTop = snapshot.autotuneIsTop;
    t.autotuneMethod = snapshot.autotuneMethod;
    t.autotuneProgress = snapshot.autotuneProgress;
    t.autotuneTargetTemp = snapshot.autotuneTargetTemp;
    t.autotuneKp = snapshot.autotuneKp;
    t.autotuneKi = snapshot.autotuneKi;
    t.autotuneKd = snapshot.autotuneKd;
    return t;
}

} // anonymous namespace

// ============================================================================
// Core 0 Live WebSocket Telemetry Broadcast Task Loop (2 Hz)
// ============================================================================

void webTask(void* pvParameters) {
    auto* ctx = static_cast<WebTaskContext*>(pvParameters);
    ESP_LOGI(TAG, "Web Telemetry Task started on Core %d (Priority %d, Period: 500 ms)", xPortGetCoreID(),
             static_cast<int>(uxTaskPriorityGet(nullptr)));

    // Register with ESP-IDF Task Watchdog Timer
    esp_task_wdt_add(nullptr);

    constexpr TickType_t PERIOD_TICKS = pdMS_TO_TICKS(500); // 2 Hz (500 ms)

    while (true) {
        esp_task_wdt_reset();

        // 1. Take atomic snapshot of live SystemContext and map to TelemetryData
        SystemContextData snapshot = ctx->context.getSnapshot();
        web::TelemetryData t = buildTelemetryData(snapshot);

        // 2. Broadcast to all active WebSocket clients
        ctx->webServer.broadcastTelemetry(t);

        // 3. Process deferred settings commit to Flash once active heating process is idle
        auto& apiCtx = web::ApiContext::instance();
        if (apiCtx.pendingSettingsSave && apiCtx.storage != nullptr && apiCtx.settings != nullptr &&
            apiCtx.fsm != nullptr) {
            const auto st = apiCtx.fsm->getState();
            if (st == fsm::ReflowState::IDLE || st == fsm::ReflowState::DONE || st == fsm::ReflowState::BACKUP ||
                st == fsm::ReflowState::FAULT) {
                apiCtx.pendingSettingsSave = false;
                apiCtx.storage->saveSettings(*apiCtx.settings);
                ESP_LOGI(TAG, "Deferred theme/settings committed to Flash after process completion.");
            }
        }

        // 4. Relaxed relative delay (prevents cumulative drift debt during transient network latency)
        vTaskDelay(PERIOD_TICKS);
    }
}

} // namespace app
