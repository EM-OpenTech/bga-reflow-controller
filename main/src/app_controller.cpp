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
 * @file app_controller.cpp
 * @brief Implementation of Master Application Controller.
 *
 * Orchestrates component dependency injection, hardware SPI bus configuration,
 * storage mount, settings loading, and FreeRTOS dual-core task spawning.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "app_controller.hpp"
#include "web/fsm_command_queue.hpp"
#include "tasks/safety_task.hpp"
#include "tasks/burstfire_task.hpp"
#include "tasks/control_task.hpp"
#include "tasks/input_task.hpp"
#include "tasks/web_task.hpp"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char* TAG = "AppController";

namespace app {

AppController::AppController()
    : _topSensor(SPI2_HOST, config::PinConfig::CS_TOP)
    , _bottomSensor(SPI2_HOST, config::PinConfig::CS_BOTTOM)
    , _topPid(_settings.topKp, _settings.topKi, _settings.topKd)
    , _bottomPid(_settings.bottomKp, _settings.bottomKi, _settings.bottomKd)
    , _safety(_outputs)
    , _fsm(_settings, _outputs, _topPid, _bottomPid)
    , _webServer(_storage, _settings, _fsm, _wifi, _context)
{
}

AppController::~AppController()
{
}

void AppController::reloadSettingsAndPidLibrary()
{
    _storage.loadSettings(_settings);
    config::PidLibrary pidLib;
    if (_storage.loadPidLibrary(pidLib)) {
        _fsm.setPidLibrary(pidLib);
    }
    if (!_settings.pidLibraryEnabled) {
        _topPid.setTunings(_settings.topKp, _settings.topKi, _settings.topKd);
        _bottomPid.setTunings(_settings.bottomKp, _settings.bottomKi, _settings.bottomKd);
    }
    _topBurst.setWindowMs(_settings.topBurstWindowMs);
    _bottomBurst.setWindowMs(_settings.bottomBurstWindowMs);

    sensor::MAX31856Config topCfg;
    topCfg.cjOffset         = _settings.topCjOffset;
    topCfg.emaFilterEnabled = _settings.emaFilterEnabled;
    topCfg.emaAlpha         = _settings.emaAlpha;
    topCfg.faultStreakLimit = _settings.faultStreakLimit;
    _topSensor.applyConfig(topCfg);

    sensor::MAX31856Config botCfg;
    botCfg.cjOffset         = _settings.bottomCjOffset;
    botCfg.emaFilterEnabled = _settings.emaFilterEnabled;
    botCfg.emaAlpha         = _settings.emaAlpha;
    botCfg.faultStreakLimit = _settings.faultStreakLimit;
    _bottomSensor.applyConfig(botCfg);

    // Update safety watchdog config in RAM
    safety::SafetyConfig safeCfg;
    safeCfg.enableSafetyWatchdog = _settings.enableSafetyWatchdog;
    safeCfg.maxTempTop           = _settings.maxTempTop;
    safeCfg.maxTempBottom        = _settings.maxTempBottom;
    safeCfg.minTempTop           = _settings.minTempTop;
    safeCfg.minTempBottom        = _settings.minTempBottom;
    safeCfg.enableMinTempCheck   = (_settings.minTempTop > 0.0f || _settings.minTempBottom > 0.0f);
    safeCfg.enableStuckSsrCheck  = _settings.enableStuckSsrCheck;
    safeCfg.stuckSsrRiseThreshold= _settings.stuckSsrRiseThreshold;
    safeCfg.stuckSsrWindowSec    = _settings.stuckSsrWindowSec;
    safeCfg.enableNoRiseCheck    = _settings.enableNoRiseCheck;
    safeCfg.noRiseThreshold      = _settings.noRiseThreshold;
    safeCfg.noRiseTimeoutSec     = _settings.noRiseTimeoutSec;
    _safety.updateConfig(safeCfg);

    ESP_LOGI(TAG, "Reloaded MachineSettings, PID Library & Sensor CJTO Offsets into RAM.");
}

bool AppController::initSpiBus()
{
    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num     = config::PinConfig::SPI_MISO;
    buscfg.mosi_io_num     = config::PinConfig::SPI_MOSI;
    buscfg.sclk_io_num     = config::PinConfig::SPI_SCK;
    buscfg.quadwp_io_num   = -1;
    buscfg.quadhd_io_num   = -1;
    buscfg.max_transfer_sz = 32;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI2 bus: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "SPI2 bus initialized successfully.");
    return true;
}

// ============================================================================
// Power-On Self-Test (POST) & Hardware Safety Verification
// ============================================================================

PostReport AppController::runPowerOnSelfTest()
{
    int64_t startTimeUs = esp_timer_get_time();
    PostReport report = {};

    // ------------------------------------------------------------------------
    // Stage 1: Boot-Cause, Watchdog Post-Mortem & Memory Audit
    // ------------------------------------------------------------------------
    report.resetReason = esp_reset_reason();
    switch (report.resetReason) {
        case ESP_RST_POWERON:   report.resetReasonStr = "POWERON (Cold Boot Normal)"; break;
        case ESP_RST_EXT:       report.resetReasonStr = "EXT (Reset Pin / EN Button)"; break;
        case ESP_RST_SW:        report.resetReasonStr = "SW (Software Reboot)"; break;
        case ESP_RST_PANIC:     report.resetReasonStr = "PANIC (Guru Meditation / Exception)"; break;
        case ESP_RST_INT_WDT:   report.resetReasonStr = "INT_WDT (Interrupt Watchdog Timeout)"; break;
        case ESP_RST_TASK_WDT:  report.resetReasonStr = "TASK_WDT (Task Watchdog Timeout)"; break;
        case ESP_RST_WDT:       report.resetReasonStr = "WDT (Other Watchdog Reset)"; break;
        case ESP_RST_DEEPSLEEP: report.resetReasonStr = "DEEPSLEEP (Deep Sleep Wakeup)"; break;
        case ESP_RST_BROWNOUT:  report.resetReasonStr = "BROWNOUT (Power Supply Dip)"; break;
        case ESP_RST_SDIO:      report.resetReasonStr = "SDIO (SDIO Reset)"; break;
        case ESP_RST_USB:       report.resetReasonStr = "USB (USB Peripheral Reset)"; break;
        case ESP_RST_JTAG:      report.resetReasonStr = "JTAG (JTAG Reset)"; break;
        default:                report.resetReasonStr = "UNKNOWN (Unclassified Reset)"; break;
    }

    report.wasWatchdogReset = (report.resetReason == ESP_RST_TASK_WDT ||
                               report.resetReason == ESP_RST_INT_WDT ||
                               report.resetReason == ESP_RST_WDT);
    report.wasBrownoutReset = (report.resetReason == ESP_RST_BROWNOUT);

    report.freeHeapBytes  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    report.freePsramBytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    // ------------------------------------------------------------------------
    // Stage 2: LittleFS Storage & Settings Integrity
    // ------------------------------------------------------------------------
    size_t fsTotal = 0, fsUsed = 0;
    report.littleFsOk = _storage.isMounted() && _storage.getStorageInfo(fsTotal, fsUsed);
    report.littleFsFreeBytes = (report.littleFsOk && fsTotal >= fsUsed) ? (fsTotal - fsUsed) : 0;

    // ------------------------------------------------------------------------
    // Stage 3: SPI Bus & MAX31856 Dual-Channel Live Probe
    // ------------------------------------------------------------------------
    sensor::SensorReading rTop = _topSensor.read();
    sensor::SensorReading rBot = _bottomSensor.read();

    report.topCjTemp      = rTop.coldJunction;
    report.topRawTemp     = rTop.temperature;
    report.topSensorOk    = rTop.isValid && !rTop.fault.hasFault();

    report.bottomCjTemp   = rBot.coldJunction;
    report.bottomRawTemp  = rBot.temperature;
    report.bottomSensorOk = rBot.isValid && !rBot.fault.hasFault();

    // Initial-seeding of lock-free sensor snapshot mailbox for Core 1 readers
    publishSensorSnapshot(rTop, rBot);

    // ------------------------------------------------------------------------
    // Stage 4: GPIO Actuator Safe-State & Inhibit Verification
    // ------------------------------------------------------------------------
    report.outputsSafe = (!_outputs.getSsrTopState() && !_outputs.getSsrBottomState());

    // ------------------------------------------------------------------------
    // Overall Result
    // ------------------------------------------------------------------------
    report.allPassed = report.littleFsOk && report.topSensorOk && report.bottomSensorOk && report.outputsSafe;

    int64_t durationMs = (esp_timer_get_time() - startTimeUs) / 1000;

    // ------------------------------------------------------------------------
    // Formatted Terminal POST Report
    // ------------------------------------------------------------------------
    ESP_LOGI(TAG, "=======================================================");
    ESP_LOGI(TAG, "            POWER-ON SELF-TEST (POST)                  ");
    ESP_LOGI(TAG, "=======================================================");
    ESP_LOGI(TAG, "[BOOT-CAUSE] Reset Reason: %s", report.resetReasonStr);
    if (report.wasWatchdogReset) {
        ESP_LOGW(TAG, "[WATCHDOG]   WARNING: Previous reboot was caused by Watchdog Timeout!");
    }
    if (report.wasBrownoutReset) {
        ESP_LOGE(TAG, "[BROWNOUT]   ALARM: Previous reboot was caused by Voltage Dip (Brownout)!");
    }
    ESP_LOGI(TAG, "[MEMORY]     Heap: %zu KB free | PSRAM: %zu KB free",
             report.freeHeapBytes / 1024, report.freePsramBytes / 1024);
    ESP_LOGI(TAG, "[STORAGE]    LittleFS: %s (%zu KB total, %zu KB free)",
             report.littleFsOk ? "MOUNTED" : "ERROR", fsTotal / 1024, report.littleFsFreeBytes / 1024);
    ESP_LOGI(TAG, "[SENSOR TOP] MAX31856 CS=%d -> %s (CJ: %.1f°C, TC: %.1f°C, Fault: 0x%02X)",
             config::PinConfig::CS_TOP, report.topSensorOk ? "OK" : "FAULT",
             report.topCjTemp, report.topRawTemp, rTop.fault.rawByte);
    ESP_LOGI(TAG, "[SENSOR BOT] MAX31856 CS=%d -> %s (CJ: %.1f°C, TC: %.1f°C, Fault: 0x%02X)",
             config::PinConfig::CS_BOTTOM, report.bottomSensorOk ? "OK" : "FAULT",
             report.bottomCjTemp, report.bottomRawTemp, rBot.fault.rawByte);
    ESP_LOGI(TAG, "[ACTUATORS]  SSR Top=%s | SSR Bot=%s | Inhibit=%s",
             _outputs.getSsrTopState() ? "ON" : "OFF",
             _outputs.getSsrBottomState() ? "ON" : "OFF",
             _outputs.isInhibited() ? "ACTIVE" : "RELEASED");
    ESP_LOGI(TAG, "=======================================================");
    if (report.allPassed) {
        ESP_LOGI(TAG, " POST RESULT: PASSED (All hardware verified in %lld ms)", durationMs);
    } else {
        ESP_LOGW(TAG, " POST RESULT: WARNING / DEGRADED (Verified in %lld ms)", durationMs);
    }
    ESP_LOGI(TAG, "=======================================================");

    return report;
}

// ============================================================================
// Master Application Initialization & Subsystem Bootstrap
// ============================================================================

bool AppController::begin()
{
    ESP_LOGI(TAG, "=== Initializing BGA Reflow Controller ===");

    // ------------------------------------------------------------------------
    // Phase 1: Shared SystemContext Mutex
    // ------------------------------------------------------------------------
    if (!_context.begin()) {
        ESP_LOGE(TAG, "Failed to initialize SystemContext mutex");
        return false;
    }

    // ------------------------------------------------------------------------
    // Phase 1b: FSM Command Queue (Core 0 → Core 1 thread-safe bridge)
    // ------------------------------------------------------------------------
    if (!fsmCmdQueueInit()) {
        ESP_LOGE(TAG, "Failed to create FSM command queue");
        return false;
    }
    ESP_LOGI(TAG, "FSM command queue created (depth 4).");

    // ------------------------------------------------------------------------
    // Phase 2: LittleFS Storage & Settings
    // ------------------------------------------------------------------------
    if (_storage.begin("/littlefs", "littlefs") != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed!");
        return false;
    }
    _storage.loadSettings(_settings);
    ESP_LOGI(TAG, "Settings loaded from Flash (PID enabled: %d, defaultProfile: '%s')",
             _settings.pidLibraryEnabled, _settings.defaultProfile.c_str());

    if (!_settings.defaultProfile.empty()) {
        config::ReflowProfile defaultProf;
        if (_storage.loadProfile(_settings.defaultProfile, defaultProf)) {
            _fsm.setActiveProfile(defaultProf);
            ESP_LOGI(TAG, "Default profile '%s' preloaded into FSM.", _settings.defaultProfile.c_str());
        }
    }

    config::PidLibrary pidLib;
    if (_storage.loadPidLibrary(pidLib)) {
        _fsm.setPidLibrary(pidLib);
        ESP_LOGI(TAG, "PID Library loaded from Flash (%zu top points, %zu bot points)",
                 pidLib.top.size(), pidLib.bottom.size());
    }

    // ------------------------------------------------------------------------
    // Phase 3: Hardware Actuators (SSR, Fan, Lamp, Buzzer)
    // ------------------------------------------------------------------------
    ESP_LOGI(TAG, "Phase 3: Initializing OutputManager...");
    _outputs.begin();

    // ------------------------------------------------------------------------
    // Phase 4: Hardware Inputs (Start, Stop, Fan/Lamp Switches)
    // ------------------------------------------------------------------------
    ESP_LOGI(TAG, "Phase 4: Initializing InputManager...");
    _inputs.begin();

    // ------------------------------------------------------------------------
    // Phase 5: SPI Bus & Temperature Sensors (MAX31856)
    // ------------------------------------------------------------------------
    ESP_LOGI(TAG, "Phase 5: Initializing SPI Sensors...");
    if (initSpiBus()) {
        // Parallel init: start hardware for both channels without blocking serially
        _topSensor.begin(true);
        _bottomSensor.begin(true);

        // Single shared wait for initial Delta-Sigma conversion
        uint32_t waitMs = _topSensor.conversionTimeMs() + 50u;
        ESP_LOGI(TAG, "Waiting %u ms for parallel dual-sensor priming...", waitMs);
        vTaskDelay(pdMS_TO_TICKS(waitMs));

        // Prime filter seeds with first settled conversion
        _topSensor.read();
        _bottomSensor.read();

        sensor::MAX31856Config topCfg;
        topCfg.cjOffset         = _settings.topCjOffset;
        topCfg.emaFilterEnabled = _settings.emaFilterEnabled;
        topCfg.emaAlpha         = _settings.emaAlpha;
        topCfg.faultStreakLimit = _settings.faultStreakLimit;
        _topSensor.applyConfig(topCfg);

        sensor::MAX31856Config botCfg;
        botCfg.cjOffset         = _settings.bottomCjOffset;
        botCfg.emaFilterEnabled = _settings.emaFilterEnabled;
        botCfg.emaAlpha         = _settings.emaAlpha;
        botCfg.faultStreakLimit = _settings.faultStreakLimit;
        _bottomSensor.applyConfig(botCfg);
    }

    // ------------------------------------------------------------------------
    // Phase 5b: Power-On Self-Test (POST) & Hardware Safety Verification
    // ------------------------------------------------------------------------
    _postReport = runPowerOnSelfTest();

    web::PostReportInfo postInfo;
    postInfo.resetReason       = static_cast<int>(_postReport.resetReason);
    postInfo.resetReasonStr    = _postReport.resetReasonStr;
    postInfo.wasWatchdogReset  = _postReport.wasWatchdogReset;
    postInfo.wasBrownoutReset  = _postReport.wasBrownoutReset;
    postInfo.freeHeapBytes     = _postReport.freeHeapBytes;
    postInfo.freePsramBytes    = _postReport.freePsramBytes;
    postInfo.littleFsOk        = _postReport.littleFsOk;
    postInfo.littleFsFreeBytes = _postReport.littleFsFreeBytes;
    postInfo.topSensorOk       = _postReport.topSensorOk;
    postInfo.topCjTemp         = _postReport.topCjTemp;
    postInfo.topRawTemp        = _postReport.topRawTemp;
    postInfo.bottomSensorOk    = _postReport.bottomSensorOk;
    postInfo.bottomCjTemp      = _postReport.bottomCjTemp;
    postInfo.bottomRawTemp     = _postReport.bottomRawTemp;
    postInfo.outputsSafe       = _postReport.outputsSafe;
    postInfo.allPassed         = _postReport.allPassed;
    web::RestApi::setPostReport(postInfo);

    // ------------------------------------------------------------------------
    // Phase 6: Control Loops & Safety Watchdog
    // ------------------------------------------------------------------------
    ESP_LOGI(TAG, "Phase 6: Initializing PID & Safety Watchdog...");
    _topPid.begin();
    _bottomPid.begin();
    _topPid.setTunings(_settings.topKp, _settings.topKi, _settings.topKd);
    _bottomPid.setTunings(_settings.bottomKp, _settings.bottomKi, _settings.bottomKd);
    _topBurst.begin(_settings.topBurstWindowMs);
    _bottomBurst.begin(_settings.bottomBurstWindowMs);

    safety::SafetyConfig safeCfg;
    safeCfg.enableSafetyWatchdog = _settings.enableSafetyWatchdog;
    safeCfg.maxTempTop           = _settings.maxTempTop;
    safeCfg.maxTempBottom        = _settings.maxTempBottom;
    safeCfg.minTempTop           = _settings.minTempTop;
    safeCfg.minTempBottom        = _settings.minTempBottom;
    safeCfg.enableMinTempCheck   = (_settings.minTempTop > 0.0f || _settings.minTempBottom > 0.0f);
    safeCfg.enableStuckSsrCheck  = _settings.enableStuckSsrCheck;
    safeCfg.stuckSsrRiseThreshold= _settings.stuckSsrRiseThreshold;
    safeCfg.stuckSsrWindowSec    = _settings.stuckSsrWindowSec;
    safeCfg.enableNoRiseCheck    = _settings.enableNoRiseCheck;
    safeCfg.noRiseThreshold      = _settings.noRiseThreshold;
    safeCfg.noRiseTimeoutSec     = _settings.noRiseTimeoutSec;
    _safety.updateConfig(safeCfg);
    _safety.begin();

    // ------------------------------------------------------------------------
    // Phase 7: Wi-Fi SoftAP, mDNS & WebServer
    // ------------------------------------------------------------------------
    ESP_LOGI(TAG, "Phase 7: Initializing Wi-Fi & WebServer...");
    _wifi.begin();
    _webServer.begin();

    // Confirm firmware stability to ESP-IDF bootloader (cancels automatic rollback)
    esp_ota_mark_app_valid_cancel_rollback();

    ESP_LOGI(TAG, "All sub-systems initialized successfully.");
    return true;
}

void AppController::startTasks()
{
    ESP_LOGI(TAG, "Spawning Dual-Core FreeRTOS Tasks...");

    // ------------------------------------------------------------------------
    // CORE 1: Real-Time Control & Hardware Safety
    // ------------------------------------------------------------------------
    // Priority 10: Safety Watchdog (20 Hz / 50ms)
    xTaskCreatePinnedToCore(safetyTask,     "safety_task",     4096, this, 10, &_taskHandles.safetyTask,    1);

    // Priority 8: High-Frequency SSR Burst-Fire Modulation (100 Hz / 10ms)
    xTaskCreatePinnedToCore(burstfireTask, "burstfire_task", 4096, this, 8,  &_taskHandles.burstfireTask, 1);

    // Priority 7: Synchronous Control Loop (5 Hz / 200ms)
    xTaskCreatePinnedToCore(controlTask,   "control_task",   8192, this, 7,  &_taskHandles.controlTask,   1);

    // ------------------------------------------------------------------------
    // CORE 0: Network, WebServer & User Inputs
    // ------------------------------------------------------------------------
    // Priority 6: Physical Button / Switch Polling (50 Hz / 20ms) -> Higher than Web!
    xTaskCreatePinnedToCore(inputTask,     "input_task",     4096, this, 6,  &_taskHandles.inputTask,     0);

    // Priority 5: Live WebSocket Telemetry Stream (2 Hz / 500ms)
    xTaskCreatePinnedToCore(webTask,       "web_task",       8192, this, 5,  &_taskHandles.webTask,       0);

    web::RestApi::setTaskHandles(_taskHandles);

    ESP_LOGI(TAG, "All FreeRTOS tasks running.");
}

} // namespace app
