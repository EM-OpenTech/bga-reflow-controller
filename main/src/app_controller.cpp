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
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "tasks/burstfire_task.hpp"
#include "tasks/control_task.hpp"
#include "tasks/input_task.hpp"
#include "tasks/safety_task.hpp"
#include "tasks/web_task.hpp"
#include "web/fsm_command_queue.hpp"

namespace app {

namespace {

constexpr const char* TAG = "AppController";

const char* getResetReasonDescription(esp_reset_reason_t reason) {
    switch (reason) {
    case ESP_RST_POWERON:
        return "POWERON (Cold Boot Normal)";
    case ESP_RST_EXT:
        return "EXT (Reset Pin / EN Button)";
    case ESP_RST_SW:
        return "SW (Software Reboot)";
    case ESP_RST_PANIC:
        return "PANIC (Guru Meditation / Exception)";
    case ESP_RST_INT_WDT:
        return "INT_WDT (Interrupt Watchdog Timeout)";
    case ESP_RST_TASK_WDT:
        return "TASK_WDT (Task Watchdog Timeout)";
    case ESP_RST_WDT:
        return "WDT (Other Watchdog Reset)";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP (Deep Sleep Wakeup)";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT (Power Supply Dip)";
    case ESP_RST_SDIO:
        return "SDIO (SDIO Reset)";
    case ESP_RST_USB:
        return "USB (USB Peripheral Reset)";
    case ESP_RST_JTAG:
        return "JTAG (JTAG Reset)";
    default:
        return "UNKNOWN (Unclassified Reset)";
    }
}

constexpr bool isWatchdogReset(esp_reset_reason_t reason) {
    return reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT;
}

void emergencyHardwareShutdown() {
    gpio_set_level(config::PinConfig::SSR_TOP, 0);
    gpio_set_level(config::PinConfig::SSR_BOTTOM, 0);
}

safety::SafetyConfig makeSafetyConfig(const config::MachineSettings& settings) {
    safety::SafetyConfig cfg;
    cfg.enableSafetyWatchdog = settings.enableSafetyWatchdog;
    cfg.maxTempTop = settings.maxTempTop;
    cfg.maxTempBottom = settings.maxTempBottom;
    cfg.minTempTop = settings.minTempTop;
    cfg.minTempBottom = settings.minTempBottom;
    cfg.enableMinTempCheck = (settings.minTempTop > 0.0f || settings.minTempBottom > 0.0f);
    cfg.enableStuckSsrCheck = settings.enableStuckSsrCheck;
    cfg.stuckSsrRiseThreshold = settings.stuckSsrRiseThreshold;
    cfg.stuckSsrWindowSec = settings.stuckSsrWindowSec;
    cfg.enableNoRiseCheck = settings.enableNoRiseCheck;
    cfg.noRiseThreshold = settings.noRiseThreshold;
    cfg.noRiseTimeoutSec = settings.noRiseTimeoutSec;
    return cfg;
}

sensor::MAX31856Config makeSensorConfig(float cjOffset, const config::MachineSettings& settings) {
    sensor::MAX31856Config cfg;
    cfg.cjOffset = cjOffset;
    cfg.emaFilterEnabled = settings.emaFilterEnabled;
    cfg.emaAlpha = settings.emaAlpha;
    cfg.faultStreakLimit = settings.faultStreakLimit;
    return cfg;
}

void logPostReport(const PostReport& report, size_t fsTotal, uint8_t topFaultByte, uint8_t botFaultByte, bool ssrTop,
                   bool ssrBot, bool inhibited, int64_t durationMs) {
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
    ESP_LOGI(TAG, "[MEMORY]     Heap: %zu KB free | PSRAM: %zu KB free", report.freeHeapBytes / 1024,
             report.freePsramBytes / 1024);
    ESP_LOGI(TAG, "[STORAGE]    LittleFS: %s (%zu KB total, %zu KB free)", report.littleFsOk ? "MOUNTED" : "ERROR",
             fsTotal / 1024, report.littleFsFreeBytes / 1024);
    ESP_LOGI(TAG, "[SENSOR TOP] MAX31856 CS=%d -> %s (CJ: %.1f°C, TC: %.1f°C, Fault: 0x%02X)",
             config::PinConfig::CS_TOP, report.topSensorOk ? "OK" : "FAULT", static_cast<double>(report.topCjTemp),
             static_cast<double>(report.topRawTemp), topFaultByte);
    ESP_LOGI(TAG, "[SENSOR BOT] MAX31856 CS=%d -> %s (CJ: %.1f°C, TC: %.1f°C, Fault: 0x%02X)",
             config::PinConfig::CS_BOTTOM, report.bottomSensorOk ? "OK" : "FAULT",
             static_cast<double>(report.bottomCjTemp), static_cast<double>(report.bottomRawTemp), botFaultByte);
    ESP_LOGI(TAG, "[ACTUATORS]  SSR Top=%s | SSR Bot=%s | Inhibit=%s", ssrTop ? "ON" : "OFF", ssrBot ? "ON" : "OFF",
             inhibited ? "ACTIVE" : "RELEASED");
    ESP_LOGI(TAG, "=======================================================");
    if (report.allPassed) {
        ESP_LOGI(TAG, " POST RESULT: PASSED (All hardware verified in %lld ms)", durationMs);
    } else {
        ESP_LOGW(TAG, " POST RESULT: WARNING / DEGRADED (Verified in %lld ms)", durationMs);
    }
    ESP_LOGI(TAG, "=======================================================");
}

} // anonymous namespace

AppController::AppController()
    : _settings{}, _topSensor(SPI2_HOST, config::PinConfig::CS_TOP),
      _bottomSensor(SPI2_HOST, config::PinConfig::CS_BOTTOM),
      _topPid(_settings.topKp, _settings.topKi, _settings.topKd),
      _bottomPid(_settings.bottomKp, _settings.bottomKi, _settings.bottomKd), _safety(_outputs),
      _fsm(_settings, _outputs, _topPid, _bottomPid), _webServer(_storage, _settings, _fsm, _wifi, _context),
      _burstfireTaskContext{_topBurst, _bottomBurst, _outputs}, _webTaskContext{_context, _webServer},
      _inputTaskContext{_inputs, _safety, _context, _storage, _fsm} {}

void AppController::applySettingsToSubsystems() {
    if (!_settings.pidLibraryEnabled) {
        _topPid.setTunings(_settings.topKp, _settings.topKi, _settings.topKd);
        _bottomPid.setTunings(_settings.bottomKp, _settings.bottomKi, _settings.bottomKd);
    }

    _topBurst.setWindowMs(_settings.topBurstWindowMs);
    _bottomBurst.setWindowMs(_settings.bottomBurstWindowMs);

    _topSensor.applyConfig(makeSensorConfig(_settings.topCjOffset, _settings));
    _bottomSensor.applyConfig(makeSensorConfig(_settings.bottomCjOffset, _settings));

    _safety.updateConfig(makeSafetyConfig(_settings));
    _outputs.setSimulationMode(_settings.simulationMode);
    _fsm.getBuzzer().setEnabled(_settings.hardwareBuzzerEnabled);
}

void AppController::reloadSettingsAndPidLibrary() {
    _storage.loadSettings(_settings);

    config::PidLibrary pidLib;
    if (_storage.loadPidLibrary(pidLib)) {
        _fsm.setPidLibrary(pidLib);
    }

    applySettingsToSubsystems();

    ESP_LOGI(TAG, "Reloaded MachineSettings, PID Library & Sensor CJTO Offsets into RAM.");
}

bool AppController::initSpiBus() {
    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num = config::PinConfig::SPI_MISO;
    buscfg.mosi_io_num = config::PinConfig::SPI_MOSI;
    buscfg.sclk_io_num = config::PinConfig::SPI_SCK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 32;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI2 bus: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "SPI2 bus initialized successfully (Polling mode, DMA disabled).");
    return true;
}

void AppController::initSensors() {
    ESP_LOGI(TAG, "Phase 5: Initializing SPI Sensors...");
    if (!initSpiBus()) {
        ESP_LOGE(TAG, "Phase 5: SPI Bus initialization failed – skipping sensor begin");
        return;
    }

    // Pre-load loaded MachineSettings (CJTO calibration offsets & filter parameters)
    // BEFORE begin() so hardware is initialized and verified directly with target settings (no double-reconfig).
    _topSensor.applyConfig(makeSensorConfig(_settings.topCjOffset, _settings));
    _bottomSensor.applyConfig(makeSensorConfig(_settings.bottomCjOffset, _settings));

    // Parallel init: start hardware for both channels without blocking serially
    _topSensor.begin(true);
    _bottomSensor.begin(true);

    // Single shared wait for initial Delta-Sigma conversion
    uint32_t waitMs = _topSensor.conversionTimeMs() + 50u;
    ESP_LOGI(TAG, "Waiting %u ms for parallel dual-sensor priming...", waitMs);
    vTaskDelay(pdMS_TO_TICKS(waitMs));

    // Note: The first actual sensor reads and filter seeding occur cleanly in Phase 5b (runPowerOnSelfTest),
    // maintaining a single-writer flow without duplicate reads or discarded samples.
}

// ============================================================================
// Power-On Self-Test (POST) & Hardware Safety Verification
// ============================================================================

PostReport AppController::runPowerOnSelfTest() {
    const int64_t startTimeUs = esp_timer_get_time();
    PostReport report = {};

    // 1. Audit Boot-Cause, Watchdog Post-Mortem & Memory
    report.resetReason = esp_reset_reason();
    report.resetReasonStr = getResetReasonDescription(report.resetReason);
    report.wasWatchdogReset = isWatchdogReset(report.resetReason);
    report.wasBrownoutReset = (report.resetReason == ESP_RST_BROWNOUT);
    report.freeHeapBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    report.freePsramBytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    // 2. Audit LittleFS Storage & Settings Integrity
    size_t fsTotal = 0;
    size_t fsUsed = 0;
    report.littleFsOk = _storage.isMounted() && _storage.getStorageInfo(fsTotal, fsUsed);
    report.littleFsFreeBytes = (report.littleFsOk && fsTotal >= fsUsed) ? (fsTotal - fsUsed) : 0;

    // 3. Probe SPI Bus & MAX31856 Dual-Channel Live Sensors (or Synthetic Sensors in Simulation Mode)
    constexpr auto makeSimulatedReading = []() constexpr -> sensor::SensorReading {
        sensor::SensorReading r{};
        r.temperature = 25.0f;
        r.rawTemperature = 25.0f;
        r.coldJunction = 25.0f;
        r.isValid = true;
        return r;
    };
    const sensor::SensorReading rTop = _settings.simulationMode ? makeSimulatedReading() : _topSensor.read();
    const sensor::SensorReading rBot = _settings.simulationMode ? makeSimulatedReading() : _bottomSensor.read();
    report.topCjTemp = rTop.coldJunction;
    report.topRawTemp = rTop.temperature;
    report.topSensorOk = rTop.isValid && !rTop.fault.hasFault();
    report.bottomCjTemp = rBot.coldJunction;
    report.bottomRawTemp = rBot.temperature;
    report.bottomSensorOk = rBot.isValid && !rBot.fault.hasFault();

    publishSensorSnapshot(rTop, rBot);

    // 4. Verify GPIO Actuator Safe-State & Inhibit
    report.outputsSafe = (!_outputs.getSsrTopState() && !_outputs.getSsrBottomState());
    report.allPassed = report.littleFsOk && report.topSensorOk && report.bottomSensorOk && report.outputsSafe;

    const int64_t durationMs = (esp_timer_get_time() - startTimeUs) / 1000;
    logPostReport(report, fsTotal, rTop.fault.rawByte, rBot.fault.rawByte, _outputs.getSsrTopState(),
                  _outputs.getSsrBottomState(), _outputs.isInhibited(), durationMs);

    return report;
}

// ============================================================================
// Master Application Initialization & Subsystem Bootstrap
// ============================================================================

bool AppController::begin() {
    ESP_LOGI(TAG, "=== Initializing BGA Reflow Controller ===");

    // Phase 1: Shared SystemContext Mutex
    if (!_context.begin()) {
        ESP_LOGE(TAG, "Failed to initialize SystemContext mutex");
        return false;
    }

    // Phase 1b: FSM Command Queue (Core 0 → Core 1 thread-safe bridge)
    if (!fsmCmdQueueInit()) {
        ESP_LOGE(TAG, "Failed to create FSM command queue");
        return false;
    }
    ESP_LOGI(TAG, "FSM command queue created (depth 4).");

    // Phase 2: LittleFS Storage & Settings
    if (_storage.begin("/littlefs", "littlefs") != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed!");
        return false;
    }
    _storage.loadSettings(_settings);
    ESP_LOGI(TAG, "Settings loaded from Flash (PID enabled: %d, defaultProfile: '%s')", _settings.pidLibraryEnabled,
             _settings.defaultProfile.c_str());

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
        ESP_LOGI(TAG, "PID Library loaded from Flash (%zu top points, %zu bot points)", pidLib.top.size(),
                 pidLib.bottom.size());
    }

    // Phase 3: Hardware Actuators (SSR, Fan, Lamp, Buzzer)
    ESP_LOGI(TAG, "Phase 3: Initializing OutputManager & BuzzerPlayer...");
    if (!_outputs.begin()) {
        ESP_LOGE(TAG, "Failed to initialize OutputManager GPIOs!");
        return false;
    }
    _outputs.setSimulationMode(_settings.simulationMode);
    _fsm.getBuzzer().begin();
    _fsm.getBuzzer().setEnabled(_settings.hardwareBuzzerEnabled);

    // Register hardware fail-safe callback: forces SSR pins LOW if an unhandled panic/reset occurs
    const esp_err_t err = esp_register_shutdown_handler(emergencyHardwareShutdown);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register emergency shutdown handler: %s", esp_err_to_name(err));
    }

    // Phase 4: Hardware Inputs (Start, Stop, Fan/Lamp Switches)
    ESP_LOGI(TAG, "Phase 4: Initializing InputManager...");
    if (!_inputs.begin()) {
        ESP_LOGE(TAG, "Failed to initialize InputManager GPIOs!");
        return false;
    }

    // Phase 5: SPI Bus & Temperature Sensors (MAX31856)
    initSensors();

    // Phase 5b: Power-On Self-Test (POST) & Hardware Safety Verification
    _postReport = runPowerOnSelfTest();
    web::RestApi::setPostReport(_postReport);

    // Phase 6: Control Loops & Safety Watchdog
    ESP_LOGI(TAG, "Phase 6: Initializing PID & Safety Watchdog...");
    _topPid.begin();
    _bottomPid.begin();
    _topPid.setTunings(_settings.topKp, _settings.topKi, _settings.topKd);
    _bottomPid.setTunings(_settings.bottomKp, _settings.bottomKi, _settings.bottomKd);
    _topBurst.begin(_settings.topBurstWindowMs);
    _bottomBurst.begin(_settings.bottomBurstWindowMs);

    _safety.updateConfig(makeSafetyConfig(_settings));
    _safety.begin();

    // Phase 7: Wi-Fi SoftAP, mDNS & WebServer
    ESP_LOGI(TAG, "Phase 7: Initializing Wi-Fi & WebServer...");
    _wifi.begin();
    _webServer.begin();

    // Confirm firmware stability to ESP-IDF bootloader (cancels automatic rollback)
    esp_ota_mark_app_valid_cancel_rollback();

    ESP_LOGI(TAG, "All sub-systems initialized successfully.");
    return true;
}

void AppController::startTasks() {
    ESP_LOGI(TAG, "Spawning Dual-Core FreeRTOS Tasks...");

    // Core 1: Real-Time Control & Hardware Safety
    // Priority 10: Safety Watchdog (20 Hz / 50ms)
    xTaskCreatePinnedToCore(safetyTask, "safety_task", 4096, this, 10, &_taskHandles.safetyTask, 1);

    // Priority 8: High-Frequency SSR Burst-Fire Modulation (100 Hz / 10ms)
    xTaskCreatePinnedToCore(burstfireTask, "burstfire_task", 4096, &_burstfireTaskContext, 8,
                            &_taskHandles.burstfireTask, 1);

    // Priority 7: Synchronous Control Loop (5 Hz / 200ms)
    xTaskCreatePinnedToCore(controlTask, "control_task", 8192, this, 7, &_taskHandles.controlTask, 1);

    // Core 0: Network, WebServer & User Inputs
    // Priority 6: Physical Button / Switch Polling (50 Hz / 20ms) -> Higher than Web!
    xTaskCreatePinnedToCore(inputTask, "input_task", 4096, &_inputTaskContext, 6, &_taskHandles.inputTask, 0);

    // Priority 5: Live WebSocket Telemetry Stream (2 Hz / 500ms)
    xTaskCreatePinnedToCore(webTask, "web_task", 8192, &_webTaskContext, 5, &_taskHandles.webTask, 0);

    web::RestApi::setTaskHandles(_taskHandles);

    ESP_LOGI(TAG, "All FreeRTOS tasks running.");
}

} // namespace app
