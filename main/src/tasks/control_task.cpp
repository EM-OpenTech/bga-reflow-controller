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
 * @file control_task.cpp
 * @brief Implementation of Core 1 5 Hz Synchronous Control Loop Task.
 *
 * Executes SPI temperature sampling, FSM state stepping, QuickPID computation,
 * burst-fire power updates, and thread-safe SystemContext synchronization.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "tasks/control_task.hpp"
#include "app_controller.hpp"
#include "web/fsm_command_queue.hpp"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char* TAG = "ControlTask";

namespace app {

// ============================================================================
// Core 1 Real-Time Synchronous Control Loop Task (5 Hz / 200 ms)
// Governed by Central System Timing (config::Timing::CONTROL_LOOP_PERIOD_MS)
// ============================================================================

void controlTask(void* pvParameters)
{
    auto* app = static_cast<AppController*>(pvParameters);
    ESP_LOGI(TAG, "Control Task started on Core %d (Priority %d, Period: %lu ms / %lu Hz)",
             xPortGetCoreID(), (int)uxTaskPriorityGet(nullptr),
             (unsigned long)config::Timing::CONTROL_LOOP_PERIOD_MS,
             (unsigned long)(1000UL / config::Timing::CONTROL_LOOP_PERIOD_MS));

    // Register with ESP-IDF Task Watchdog Timer
    esp_task_wdt_add(NULL);

    constexpr uint32_t DT_MS = config::Timing::CONTROL_LOOP_PERIOD_MS;
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(DT_MS);

    while (true) {
        esp_task_wdt_reset();

        // --------------------------------------------------------------------
        // 1. Read Sensors or Update Thermal Simulation
        // --------------------------------------------------------------------
        float topTemp = 25.0f;
        float botTemp = 25.0f;
        sensor::SensorReading topReading;
        sensor::SensorReading botReading;

        if (app->getSettings().simulationMode) {
            float lastTopPower = app->getTopPid().getOutput();
            float lastBotPower = app->getBottomPid().getOutput();
            bool fanRunning = app->getFsm().getFanEffective();
            app->getSimulator().update(lastTopPower, lastBotPower, fanRunning, DT_MS);
            topTemp = app->getSimulator().getTopTemperature();
            botTemp = app->getSimulator().getBottomTemperature();

            topReading.temperature    = topTemp;
            topReading.rawTemperature = topTemp;
            topReading.coldJunction   = 25.0f;
            topReading.isValid        = true;
            topReading.timestampMs    = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

            botReading.temperature    = botTemp;
            botReading.rawTemperature = botTemp;
            botReading.coldJunction   = 25.0f;
            botReading.isValid        = true;
            botReading.timestampMs    = topReading.timestampMs;
        } else {
            topReading = app->getTopSensor().read();
            botReading = app->getBottomSensor().read();
            topTemp = topReading.temperature;
            botTemp = botReading.temperature;
        }

        // --------------------------------------------------------------------
        // 2. Drain FSM Command Queue (thread-safe Core 0 → Core 1)
        // --------------------------------------------------------------------
        {
            FsmCommand cmd;
            while (xQueueReceive(g_fsmCmdQueue, &cmd, 0) == pdTRUE) {
                switch (cmd.type) {
                case FsmCommandType::PREHEAT:
                    // Profile was loaded on Core 0 and heap-allocated.
                    // We own the pointer – call startPreheat then free it.
                    if (cmd.profile != nullptr) {
                        auto st = app->getFsm().getState();
                        if (st == fsm::ReflowState::IDLE || st == fsm::ReflowState::DONE || st == fsm::ReflowState::COOLING) {
                            app->reloadSettingsAndPidLibrary();
                            if (app->getContext().lock(10)) {
                                app->getContext().getData().history.clear();
                                app->getContext().unlock();
                            }
                            app->getFsm().startPreheat(*cmd.profile);
                            ESP_LOGI(TAG, "CMD: PREHEAT executed");
                        } else {
                            ESP_LOGW(TAG, "CMD: PREHEAT ignored – not permitted in state: %s", app->getFsm().getStateString());
                        }
                        delete cmd.profile;
                        cmd.profile = nullptr;
                    } else {
                        ESP_LOGE(TAG, "CMD: PREHEAT – null profile pointer!");
                    }
                    break;
                case FsmCommandType::REFLOW:
                    app->getFsm().startReflow();
                    ESP_LOGI(TAG, "CMD: REFLOW");
                    break;
                case FsmCommandType::STOP:
                    app->getFsm().stop();
                    ESP_LOGI(TAG, "CMD: STOP");
                    break;
                case FsmCommandType::SKIP_STEP:
                    app->getFsm().skipStep();
                    ESP_LOGI(TAG, "CMD: SKIP_STEP");
                    break;
                case FsmCommandType::RESET_FAULT:
                    // @todo: Evaluate and implement synchronous app->getSafety().reset() call here
                    //        once dedicated safety fault clearing logic and validation are ready.
                    //        Currently, FSM resetFault() clears the process state machine (→ IDLE),
                    //        while SafetyWatchdog hardware inhibit remains active until explicitly reset.
                    app->getFsm().resetFault();
                    ESP_LOGI(TAG, "CMD: RESET_FAULT (FSM state cleared; safety hardware inhibit maintained)");
                    break;
                case FsmCommandType::AUTOTUNE_START:
                    app->reloadSettingsAndPidLibrary();
                    if (app->getContext().lock(10)) {
                        app->getContext().getData().history.clear();
                        app->getContext().unlock();
                    }
                    app->getFsm().startAutotune(cmd.tuneIsTop, cmd.tuneTargetTemp);
                    ESP_LOGI(TAG, "CMD: AUTOTUNE_START (channel=%s, temp=%.1f°C)",
                             cmd.tuneIsTop ? "TOP" : "BOTTOM", cmd.tuneTargetTemp);
                    break;
                case FsmCommandType::AUTOTUNE_STOP:
                    app->getFsm().stopAutotune();
                    ESP_LOGI(TAG, "CMD: AUTOTUNE_STOP");
                    break;
                case FsmCommandType::ENTER_BACKUP:
                    app->getFsm().enterBackupState();
                    ESP_LOGI(TAG, "CMD: ENTER_BACKUP");
                    break;
                case FsmCommandType::EXIT_BACKUP:
                    app->getFsm().exitBackupState();
                    ESP_LOGI(TAG, "CMD: EXIT_BACKUP");
                    break;
                case FsmCommandType::RELOAD_SETTINGS: {
                    auto st = app->getFsm().getState();
                    if (st == fsm::ReflowState::IDLE || st == fsm::ReflowState::DONE) {
                        app->reloadSettingsAndPidLibrary();
                        ESP_LOGI(TAG, "CMD: RELOAD_SETTINGS applied to RAM & subsystems in IDLE.");
                    } else {
                        ESP_LOGI(TAG, "CMD: RELOAD_SETTINGS deferred – active process in state: %s", app->getFsm().getStateString());
                    }
                    break;
                }
                }
            }
        }

        // --------------------------------------------------------------------
        // 3. Tick State Machine (Governed by DT_MS)
        // --------------------------------------------------------------------
        static fsm::ReflowState s_prevFsmState = fsm::ReflowState::IDLE;
        app->getFsm().update(topTemp, botTemp, DT_MS);

        fsm::ReflowState currentFsmState = app->getFsm().getState();
        if (s_prevFsmState == fsm::ReflowState::AUTOTUNE && currentFsmState != fsm::ReflowState::AUTOTUNE) {
            // If AUTOTUNE just finished successfully, save the new PID gains to LittleFS FIRST
            // before entering COOLING / IDLE!
            if (app->getFsm().getAutotuner().isFinished()) {
                app->getStorage().savePidLibrary(app->getFsm().getPidLibrary());
                ESP_LOGI(TAG, "AUTOTUNE completed – auto-saved new PID gains to LittleFS.");
            }
        }
        if (s_prevFsmState != fsm::ReflowState::IDLE && currentFsmState == fsm::ReflowState::IDLE) {
            // Returned to IDLE from an active cycle – reload any deferred settings/PID library changes
            app->reloadSettingsAndPidLibrary();
        }
        s_prevFsmState = currentFsmState;

        // --------------------------------------------------------------------
        // 4. Compute PID Outputs
        // --------------------------------------------------------------------
        // (Setpoints and inputs are set directly inside FSM update)
        app->getTopPid().compute();
        app->getBottomPid().compute();

        float topPower = app->getTopPid().getOutput();
        float botPower = app->getBottomPid().getOutput();

        // --------------------------------------------------------------------
        // 4. Provide PID Power to BurstFire Controllers
        // --------------------------------------------------------------------
        // (SSR GPIO outputs are high-frequency modulated at 100 Hz / 10ms in burstfire_task)
        app->getTopBurst().setPower(topPower);
        app->getBottomBurst().setPower(botPower);

        bool topSsrOn = app->getTopBurst().getState();
        bool botSsrOn = app->getBottomBurst().getState();

        // Publish synchronized atomic snapshot for safety_task (Single-Writer, lock-free)
        app->publishSensorSnapshot(topReading, botReading,
                                   topPower, botPower,
                                   app->getFsm().getTopSetpoint(),
                                   app->getFsm().getBottomSetpoint());

        // --------------------------------------------------------------------
        // 5. Update Shared SystemContext for Core 0
        // --------------------------------------------------------------------
        if (app->getContext().lock(10)) {
            auto& ctx = app->getContext().getData();
            ctx.topTemp          = topTemp;
            ctx.bottomTemp       = botTemp;
            ctx.topSensorOk      = topReading.isValid;
            ctx.bottomSensorOk   = botReading.isValid;
            ctx.state            = app->getFsm().getState();
            ctx.stateStr         = app->getFsm().getStateString();
            ctx.activeProfileFile = app->getFsm().getActiveProfileFile();
            ctx.preheatDone      = app->getFsm().isPreheatDone();
            ctx.elapsedSec       = app->getFsm().getElapsedSec();
            ctx.talSec           = app->getFsm().getTalSec();
            ctx.topSetpoint      = app->getFsm().getTopSetpoint();
            ctx.bottomSetpoint   = app->getFsm().getBottomSetpoint();
            ctx.topPower         = topPower;
            ctx.bottomPower      = botPower;
            ctx.fanActive        = app->getFsm().getFanEffective();
            ctx.lampActive       = app->getFsm().getLampEffective();
            ctx.ssrTopActive     = topSsrOn;
            ctx.ssrBottomActive  = botSsrOn;
            ctx.topStep          = app->getFsm().getTopStepIndex();
            ctx.bottomStep       = app->getFsm().getBotStepIndex();
            ctx.topSettling      = app->getFsm().isTopSettling();
            ctx.bottomSettling   = app->getFsm().isBottomSettling();
            ctx.topHolding       = app->getFsm().isTopHolding();
            ctx.bottomHolding    = app->getFsm().isBottomHolding();
            ctx.topSettleRemain    = app->getFsm().getTopSettleRemainSec();
            ctx.bottomSettleRemain = app->getFsm().getBotSettleRemainSec();
            ctx.topHoldRemain      = app->getFsm().getTopHoldRemainSec();
            ctx.bottomHoldRemain   = app->getFsm().getBotHoldRemainSec();

            // Active PID Gains
            ctx.topPidKp           = app->getTopPid().getKp();
            ctx.topPidKi           = app->getTopPid().getKi();
            ctx.topPidKd           = app->getTopPid().getKd();
            ctx.bottomPidKp        = app->getBottomPid().getKp();
            ctx.bottomPidKi        = app->getBottomPid().getKi();
            ctx.bottomPidKd        = app->getBottomPid().getKd();

            // Autotune Telemetry
            ctx.autotuneActive     = (app->getFsm().getState() == fsm::ReflowState::AUTOTUNE);
            ctx.autotuneFinished   = app->getFsm().getAutotuner().isFinished();
            ctx.autotuneIsTop      = app->getFsm().getAutotuner().isTop();
            ctx.autotuneProgress   = app->getFsm().getAutotuner().getProgressPercent();
            ctx.autotuneTargetTemp = app->getFsm().getAutotuner().getTargetTemp();
            if (ctx.autotuneFinished) {
                float kp, ki, kd;
                app->getFsm().getAutotuner().getResults(kp, ki, kd);
                ctx.autotuneKp = kp;
                ctx.autotuneKi = ki;
                ctx.autotuneKd = kd;
            } else {
                ctx.autotuneKp = 0.0f;
                ctx.autotuneKi = 0.0f;
                ctx.autotuneKd = 0.0f;
            }

            // Copy Step Markers
            ctx.stepMarkers.clear();
            const auto* markers = app->getFsm().getStepMarkers();
            size_t count = app->getFsm().getMarkerCount();
            for (size_t i = 0; i < count; ++i) {
                ctx.stepMarkers.push_back(markers[i]);
            }

            // Record History Buffer for active process (every 1 second)
            static uint32_t s_lastRecordedSec = 0xFFFFFFFF;
            auto fsmState = app->getFsm().getState();
            if (fsmState == fsm::ReflowState::IDLE) {
                // In IDLE: do not record new points, but KEEP history buffer of last completed run intact!
                s_lastRecordedSec = 0xFFFFFFFF;
            } else if (fsmState == fsm::ReflowState::PREHEAT ||
                       fsmState == fsm::ReflowState::SOAK ||
                       fsmState == fsm::ReflowState::REFLOW ||
                       fsmState == fsm::ReflowState::COOLING ||
                       fsmState == fsm::ReflowState::DONE ||
                       fsmState == fsm::ReflowState::AUTOTUNE) {
                if (ctx.elapsedSec != s_lastRecordedSec) {
                    if (ctx.elapsedSec == 0 && s_lastRecordedSec == 0xFFFFFFFF) {
                        ctx.history.clear();
                    }
                    s_lastRecordedSec = ctx.elapsedSec;
                    if (ctx.history.size() < config::Limits::MAX_HISTORY_POINTS) { // Up to 3 hours (10,800s)
                        ctx.history.push_back({
                            static_cast<uint16_t>(ctx.elapsedSec),
                            ctx.topTemp,
                            ctx.bottomTemp,
                            ctx.topSetpoint,
                            ctx.bottomSetpoint
                        });
                    }
                }
            }

            app->getContext().unlock();
        }

        // Wait until next 200ms cycle (5 Hz)
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

} // namespace app
