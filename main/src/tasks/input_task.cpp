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
 * @file input_task.cpp
 * @brief Implementation of Core 0 50 Hz Physical Button and Switch Polling Task.
 *
 * Evaluates hardware debouncing, short/long press durations, master switch overrides,
 * and posts verified user commands to the cross-core FSM queue.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "tasks/input_task.hpp"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "fsm/reflow_fsm.hpp"
#include "input/input_manager.hpp"
#include "safety/safety_watchdog.hpp"
#include "storage/storage_manager.hpp"
#include "system_context.hpp"
#include "web/fsm_command_queue.hpp"
#include <memory>

namespace app {

namespace {

constexpr const char* TAG = "InputTask";

void handleStartButtonRelease(InputTaskContext* ctx, uint64_t durationMs) {
    if (ctx->safety.hasFault()) {
        ESP_LOGW(TAG, "Hardware Button: START ignored – active safety fault: %s", ctx->safety.getFaultString());
        return;
    }

    if (durationMs >= 2000) {
        // Long Press (>2s): Start Preheat
        ESP_LOGI(TAG, "START Long Press (%llu ms) -> queue PREHEAT", durationMs);
        std::string profName = ctx->context.getSnapshot().activeProfileFile;
        if (profName.empty()) {
            ESP_LOGW(TAG, "Hardware Button: START pressed but no profile is selected – aborted.");
            return;
        }

        auto prof = std::make_unique<config::ReflowProfile>();
        if (!ctx->storage.loadProfile(profName, *prof)) {
            ESP_LOGW(TAG, "Hardware Button: Active profile '%s' not found in storage", profName.c_str());
            return;
        }

        FsmCommand cmd{};
        cmd.type = FsmCommandType::PREHEAT;
        cmd.profile = prof.release();
        if (!fsmCmdPost(cmd)) {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            delete cmd.profile;
            ESP_LOGE(TAG, "Hardware Button: PREHEAT queue full – profile freed");
        }
        return;
    }

    if (durationMs >= 50) {
        // Short Press (<2s): Start Reflow (after Preheat Done)
        ESP_LOGI(TAG, "START Short Press (%llu ms) -> queue REFLOW", durationMs);
        FsmCommand cmd{};
        cmd.type = FsmCommandType::REFLOW;
        fsmCmdPost(cmd);
    }
}

void handleStopButtonRelease(InputTaskContext* ctx, uint64_t durationMs) {
    if (durationMs >= 2000) {
        // Long Press (>2s): Graceful Stop (transitions to Cooling)
        ESP_LOGI(TAG, "STOP Long Press (%llu ms) -> queue STOP", durationMs);
        FsmCommand cmd{};
        cmd.type = FsmCommandType::STOP;
        fsmCmdPost(cmd);
        return;
    }

    if (durationMs < 50) {
        return;
    }

    // Short Press (<2s):
    auto state = ctx->context.getSnapshot().state;
    if (state == fsm::ReflowState::FAULT) {
        ESP_LOGI(TAG, "STOP Short Press in FAULT -> queue RESET_FAULT");
        FsmCommand cmd{};
        cmd.type = FsmCommandType::RESET_FAULT;
        fsmCmdPost(cmd);
        return;
    }

    if (state == fsm::ReflowState::PREHEAT || state == fsm::ReflowState::SOAK || state == fsm::ReflowState::REFLOW) {
        ESP_LOGI(TAG, "STOP Short Press during process -> queue SKIP_STEP");
        FsmCommand cmd{};
        cmd.type = FsmCommandType::SKIP_STEP;
        fsmCmdPost(cmd);
    }
}

} // anonymous namespace

// ============================================================================
// Core 0 Hardware Button & Switch Polling Task (50 Hz)
// ============================================================================

void inputTask(void* pvParameters) {
    auto* ctx = static_cast<InputTaskContext*>(pvParameters);
    ESP_LOGI(TAG, "Input Task started on Core %d (Priority %d, Period: 20 ms)", xPortGetCoreID(),
             static_cast<int>(uxTaskPriorityGet(nullptr)));

    // Register with ESP-IDF Task Watchdog Timer
    esp_task_wdt_add(nullptr);

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(20); // 50 Hz (20 ms)

    // Button press duration trackers
    uint64_t startPressBeginUs = 0;
    uint64_t stopPressBeginUs = 0;
    bool lastStartHeld = false;
    bool lastStopHeld = false;

    // Toggle switch callbacks setup
    ctx->inputs.setOnFanSwitchChanged([ctx](bool active) {
        ESP_LOGI(TAG, "Hardware SW_FAN toggled: %d", active);
        ctx->fsm.setFanOverride(active);
    });

    ctx->inputs.setOnLampSwitchChanged([ctx](bool active) {
        ESP_LOGI(TAG, "Hardware SW_LAMP toggled: %d", active);
        ctx->fsm.setLampOverride(active);
    });

    while (true) {
        esp_task_wdt_reset();

        // Poll and debounce physical GPIOs
        ctx->inputs.update();

        uint64_t nowUs = esp_timer_get_time();

        // 1. Start Button Logic
        bool startHeld = ctx->inputs.isStartHeld();
        if (startHeld && !lastStartHeld) {
            // Button just pressed down
            startPressBeginUs = nowUs;
        } else if (!startHeld && lastStartHeld) {
            // Button released -> evaluate press duration
            uint64_t durationMs = (nowUs - startPressBeginUs) / 1000ULL;
            handleStartButtonRelease(ctx, durationMs);
        }
        lastStartHeld = startHeld;

        // 2. Stop Button Logic
        bool stopHeld = ctx->inputs.isStopHeld();
        if (stopHeld && !lastStopHeld) {
            stopPressBeginUs = nowUs;
        } else if (!stopHeld && lastStopHeld) {
            uint64_t durationMs = (nowUs - stopPressBeginUs) / 1000ULL;
            handleStopButtonRelease(ctx, durationMs);
        }
        lastStopHeld = stopHeld;

        // Wait until next 20ms cycle
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

} // namespace app
