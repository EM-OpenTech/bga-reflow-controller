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
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "web/fsm_command_queue.hpp"
#include <memory>

namespace app {

namespace {

constexpr const char* TAG = "ControlTask";

// ============================================================================
// Internal Helper: Sensor & Simulation Acquisition
// ============================================================================
void readSensorsOrSim(AppController* app, uint32_t dtMs, float& topTemp, float& botTemp,
                      sensor::SensorReading& topReading, sensor::SensorReading& botReading) {
    if (app->getSettings().simulationMode) {
        float lastTopPower = app->getTopPid().getOutput();
        float lastBotPower = app->getBottomPid().getOutput();
        bool fanRunning = app->getFsm().getFanEffective();
        app->getSimulator().update(lastTopPower, lastBotPower, fanRunning, dtMs);
        topTemp = app->getSimulator().getTopTemperature();
        botTemp = app->getSimulator().getBottomTemperature();

        topReading.temperature = topTemp;
        topReading.rawTemperature = topTemp;
        topReading.coldJunction = 25.0f;
        topReading.isValid = true;
        topReading.timestampMs = static_cast<uint32_t>(static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL);

        botReading.temperature = botTemp;
        botReading.rawTemperature = botTemp;
        botReading.coldJunction = 25.0f;
        botReading.isValid = true;
        botReading.timestampMs = topReading.timestampMs;
    } else {
        topReading = app->getTopSensor().read();
        botReading = app->getBottomSensor().read();
        topTemp = topReading.isValid ? topReading.temperature : config::Limits::FAILSAFE_DISCONNECTED_TEMP;
        botTemp = botReading.isValid ? botReading.temperature : config::Limits::FAILSAFE_DISCONNECTED_TEMP;
    }
}

// ============================================================================
// Internal Helper: Command Dispatcher
// ============================================================================
void clearTelemetryHistory(SystemContext& context) {
    if (context.lock(10)) {
        context.getData().history.clear();
        context.unlock();
    }
}

constexpr bool isPreheatAllowed(fsm::ReflowState state) {
    return state == fsm::ReflowState::IDLE || state == fsm::ReflowState::DONE || state == fsm::ReflowState::COOLING;
}

constexpr bool isReloadAllowed(fsm::ReflowState state) {
    return state == fsm::ReflowState::IDLE || state == fsm::ReflowState::DONE || state == fsm::ReflowState::FAULT;
}

void executeCommandPreheat(AppController* app, FsmCommand& cmd) {
    std::unique_ptr<config::ReflowProfile> profile(cmd.profile);
    cmd.profile = nullptr;

    if (!profile) {
        ESP_LOGE(TAG, "CMD: PREHEAT – null profile pointer!");
        return;
    }

    if (app->getSafety().hasFault()) {
        ESP_LOGE(TAG, "CMD: PREHEAT rejected – active safety fault: %s", app->getSafety().getFaultString());
        return;
    }

    if (!app->getSettings().simulationMode) {
        sensor::SensorReading topReading;
        sensor::SensorReading botReading;
        app->getSensorSnapshot(topReading, botReading);
        if (!botReading.isValid) {
            ESP_LOGE(TAG, "CMD: PREHEAT rejected – bottom thermocouple is disconnected or invalid!");
            return;
        }
    }

    auto st = app->getFsm().getState();
    if (!isPreheatAllowed(st)) {
        ESP_LOGW(TAG, "CMD: PREHEAT ignored – not permitted in state: %s", app->getFsm().getStateString());
        return;
    }

    app->applySettingsToSubsystems();
    clearTelemetryHistory(app->getContext());
    app->getFsm().startPreheat(*profile);
    ESP_LOGI(TAG, "CMD: PREHEAT executed");
}

void executeCommandReflow(AppController* app) {
    if (app->getSafety().hasFault()) {
        ESP_LOGE(TAG, "CMD: REFLOW rejected – active safety fault: %s", app->getSafety().getFaultString());
        return;
    }
    if (!app->getSettings().simulationMode) {
        sensor::SensorReading topReading;
        sensor::SensorReading botReading;
        app->getSensorSnapshot(topReading, botReading);
        if (!topReading.isValid || !botReading.isValid) {
            ESP_LOGE(TAG, "CMD: REFLOW rejected – thermocouple sensors are disconnected or invalid!");
            return;
        }
    }
    app->getFsm().startReflow();
    ESP_LOGI(TAG, "CMD: REFLOW");
}

void executeCommandAutotune(AppController* app, const FsmCommand& cmd) {
    if (app->getSafety().hasFault()) {
        ESP_LOGE(TAG, "CMD: AUTOTUNE rejected – active safety fault: %s", app->getSafety().getFaultString());
        return;
    }
    if (!app->getSettings().simulationMode) {
        sensor::SensorReading topReading;
        sensor::SensorReading botReading;
        app->getSensorSnapshot(topReading, botReading);
        const bool sensorOk = cmd.tuneIsTop ? topReading.isValid : botReading.isValid;
        if (!sensorOk) {
            ESP_LOGE(TAG, "CMD: AUTOTUNE rejected – %s thermocouple is disconnected or invalid!",
                     cmd.tuneIsTop ? "TOP" : "BOTTOM");
            return;
        }
    }
    app->applySettingsToSubsystems();
    clearTelemetryHistory(app->getContext());
    app->getFsm().startAutotune(cmd.tuneIsTop, cmd.tuneTargetTemp);
    ESP_LOGI(TAG, "CMD: AUTOTUNE_START (channel=%s, temp=%.1f°C)", cmd.tuneIsTop ? "TOP" : "BOTTOM",
             static_cast<double>(cmd.tuneTargetTemp));
}

void executeCommandReloadSettings(AppController* app) {
    auto st = app->getFsm().getState();
    if (isReloadAllowed(st)) {
        app->applySettingsToSubsystems();
        ESP_LOGI(TAG, "CMD: RELOAD_SETTINGS applied to RAM & subsystems in %s.", app->getFsm().getStateString());
    } else {
        ESP_LOGI(TAG, "CMD: RELOAD_SETTINGS deferred – active process in state: %s", app->getFsm().getStateString());
    }
}

void handleFsmCommand(AppController* app, FsmCommand& cmd) {
    switch (cmd.type) {
    case FsmCommandType::PREHEAT:
        executeCommandPreheat(app, cmd);
        break;
    case FsmCommandType::REFLOW:
        executeCommandReflow(app);
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
        app->getSafety().reset();
        app->getFsm().resetFault();
        ESP_LOGI(TAG, "CMD: RESET_FAULT (Safety watchdog released and FSM state cleared to IDLE)");
        break;
    case FsmCommandType::AUTOTUNE_START:
        executeCommandAutotune(app, cmd);
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
    case FsmCommandType::RELOAD_SETTINGS:
        executeCommandReloadSettings(app);
        break;
    }

    // Defensive memory safety: if cmd.profile was not consumed, free it to eliminate leak risk
    if (cmd.profile != nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete cmd.profile;
        cmd.profile = nullptr;
    }
}

void drainCommandQueue(AppController* app) {
    FsmCommand cmd;
    while (xQueueReceive(g_fsmCmdQueue, &cmd, 0) == pdTRUE) {
        handleFsmCommand(app, cmd);
    }
}

// ============================================================================
// Internal Helper: History Buffer Recording
// ============================================================================
constexpr bool isProcessState(fsm::ReflowState state) {
    return state == fsm::ReflowState::PREHEAT || state == fsm::ReflowState::SOAK || state == fsm::ReflowState::REFLOW ||
           state == fsm::ReflowState::COOLING || state == fsm::ReflowState::DONE || state == fsm::ReflowState::AUTOTUNE;
}

void recordHistory(SystemContextData& ctx, fsm::ReflowState fsmState) {
    static uint32_t s_lastRecordedSec = 0xFFFFFFFF;

    if (fsmState == fsm::ReflowState::IDLE) {
        // In IDLE: do not record new points, but KEEP history buffer of last completed run intact!
        s_lastRecordedSec = 0xFFFFFFFF;
        return;
    }

    if (!isProcessState(fsmState)) {
        return;
    }

    if (ctx.elapsedSec == s_lastRecordedSec) {
        return;
    }

    if (ctx.elapsedSec == 0 && s_lastRecordedSec == 0xFFFFFFFF) {
        ctx.history.clear();
    }
    s_lastRecordedSec = ctx.elapsedSec;

    if (ctx.history.size() < config::Limits::MAX_HISTORY_POINTS) { // Up to 3 hours (10,800s)
        ctx.history.push_back(
            {static_cast<uint16_t>(ctx.elapsedSec), ctx.topTemp, ctx.bottomTemp, ctx.topSetpoint, ctx.bottomSetpoint});
    }
}

// ============================================================================
// Internal Helper: SystemContext & History Synchronization
// ============================================================================
void syncSystemContext(AppController* app, float topTemp, float botTemp, const sensor::SensorReading& topReading,
                       const sensor::SensorReading& botReading, float topPower, float botPower, bool topSsrOn,
                       bool botSsrOn) {
    if (!app->getContext().lock(10)) {
        return;
    }

    auto& ctx = app->getContext().getData();
    ctx.topTemp = topReading.isValid ? topTemp : 0.0f;
    ctx.bottomTemp = botReading.isValid ? botTemp : 0.0f;
    ctx.topSensorOk = topReading.isValid;
    ctx.bottomSensorOk = botReading.isValid;
    ctx.state = app->getFsm().getState();
    ctx.stateStr = app->getFsm().getStateString();
    ctx.activeProfileFile = app->getFsm().getActiveProfileFile();
    ctx.preheatDone = app->getFsm().isPreheatDone();
    ctx.elapsedSec = app->getFsm().getElapsedSec();
    ctx.talSec = app->getFsm().getTalSec();
    ctx.topSetpoint = app->getFsm().getTopSetpoint();
    ctx.bottomSetpoint = app->getFsm().getBottomSetpoint();
    ctx.topPower = topPower;
    ctx.bottomPower = botPower;
    ctx.fanActive = app->getFsm().getFanEffective();
    ctx.lampActive = app->getFsm().getLampEffective();
    ctx.ssrTopActive = topSsrOn;
    ctx.ssrBottomActive = botSsrOn;
    ctx.topStep = app->getFsm().getTopStepIndex();
    ctx.bottomStep = app->getFsm().getBotStepIndex();
    ctx.topSettling = app->getFsm().isTopSettling();
    ctx.bottomSettling = app->getFsm().isBottomSettling();
    ctx.topHolding = app->getFsm().isTopHolding();
    ctx.bottomHolding = app->getFsm().isBottomHolding();
    ctx.topSettleRemain = app->getFsm().getTopSettleRemainSec();
    ctx.bottomSettleRemain = app->getFsm().getBotSettleRemainSec();
    ctx.topHoldRemain = app->getFsm().getTopHoldRemainSec();
    ctx.bottomHoldRemain = app->getFsm().getBotHoldRemainSec();

    // Active PID Gains
    ctx.topPidKp = app->getTopPid().getKp();
    ctx.topPidKi = app->getTopPid().getKi();
    ctx.topPidKd = app->getTopPid().getKd();
    ctx.bottomPidKp = app->getBottomPid().getKp();
    ctx.bottomPidKi = app->getBottomPid().getKi();
    ctx.bottomPidKd = app->getBottomPid().getKd();

    // Autotune Telemetry
    ctx.autotuneActive = (app->getFsm().getState() == fsm::ReflowState::AUTOTUNE);
    ctx.autotuneFinished = app->getFsm().getAutotuner().isFinished();
    ctx.autotuneIsTop = app->getFsm().getAutotuner().isTop();
    ctx.autotuneProgress = app->getFsm().getAutotuner().getProgressPercent();
    ctx.autotuneTargetTemp = app->getFsm().getAutotuner().getTargetTemp();
    if (ctx.autotuneFinished) {
        app->getFsm().getAutotuner().getResults(ctx.autotuneKp, ctx.autotuneKi, ctx.autotuneKd);
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
    recordHistory(ctx, app->getFsm().getState());

    app->getContext().unlock();
}

} // anonymous namespace

// ============================================================================
// Core 1 Real-Time Synchronous Control Loop Task (5 Hz / 200 ms)
// Governed by Central System Timing (config::Timing::CONTROL_LOOP_PERIOD_MS)
// ============================================================================

void controlTask(void* pvParameters) {
    auto* app = static_cast<AppController*>(pvParameters);
    ESP_LOGI(TAG, "Control Task started on Core %d (Priority %d, Period: %lu ms)", xPortGetCoreID(),
             static_cast<int>(uxTaskPriorityGet(nullptr)),
             static_cast<unsigned long>(config::Timing::CONTROL_LOOP_PERIOD_MS));

    // Register with ESP-IDF Task Watchdog Timer
    esp_task_wdt_add(nullptr);

    constexpr uint32_t DT_MS = config::Timing::CONTROL_LOOP_PERIOD_MS;
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(DT_MS);
    fsm::ReflowState prevFsmState = fsm::ReflowState::IDLE;

    while (true) {
        esp_task_wdt_reset();

        // 1. Read Sensors or Update Thermal Simulation
        float topTemp = 25.0f;
        float botTemp = 25.0f;
        sensor::SensorReading topReading;
        sensor::SensorReading botReading;
        readSensorsOrSim(app, DT_MS, topTemp, botTemp, topReading, botReading);

        // 2. Drain FSM Command Queue (thread-safe Core 0 -> Core 1)
        drainCommandQueue(app);

        // 3. Tick State Machine (Governed by DT_MS)
        app->getFsm().update(topTemp, botTemp, DT_MS);

        fsm::ReflowState currentFsmState = app->getFsm().getState();
        if (prevFsmState == fsm::ReflowState::AUTOTUNE && currentFsmState != fsm::ReflowState::AUTOTUNE) {
            if (app->getFsm().getAutotuner().isFinished()) {
                app->getStorage().savePidLibrary(app->getFsm().getPidLibrary());
                ESP_LOGI(TAG, "AUTOTUNE completed – auto-saved new PID gains to LittleFS.");
            }
        }
        if (prevFsmState != fsm::ReflowState::IDLE && currentFsmState == fsm::ReflowState::IDLE) {
            app->applySettingsToSubsystems();
        }
        prevFsmState = currentFsmState;

        // 4. Compute PID Outputs & Update Actuators
        app->getTopPid().compute();
        app->getBottomPid().compute();

        float topPower = app->getTopPid().getOutput();
        float botPower = app->getBottomPid().getOutput();

        // Fail-Safe Hardware Interlock (Defense-in-Depth):
        // If a thermocouple is disconnected or invalid (!isValid), the heater power
        // MUST be clamped strictly to 0.0% and PID integral state reset,
        // even if the Safety Watchdog is disabled for testbench experiments.
        if (!topReading.isValid) {
            topPower = 0.0f;
            app->getTopPid().reset();
        }
        if (!botReading.isValid) {
            botPower = 0.0f;
            app->getBottomPid().reset();
        }

        app->getTopBurst().setPower(topPower);
        app->getBottomBurst().setPower(botPower);

        bool topSsrOn = app->getTopBurst().getState();
        bool botSsrOn = app->getBottomBurst().getState();

        // Publish synchronized atomic snapshot for safety_task (Single-Writer, lock-free)
        app->publishSensorSnapshot(topReading, botReading, topPower, botPower, app->getFsm().getTopSetpoint(),
                                   app->getFsm().getBottomSetpoint());

        // 5. Update Shared SystemContext for Core 0
        syncSystemContext(app, topTemp, botTemp, topReading, botReading, topPower, botPower, topSsrOn, botSsrOn);

        // Wait until next 200ms cycle (5 Hz)
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

} // namespace app
