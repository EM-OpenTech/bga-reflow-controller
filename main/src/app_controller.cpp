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
#include "driver/gpio.h"

static const char* TAG = "AppController";

namespace app {

AppController::AppController()
    : _topSensor(SPI2_HOST, config::PinConfig::CS_TOP)
    , _bottomSensor(SPI2_HOST, config::PinConfig::CS_BOTTOM)
    , _topPid(2.5f, 0.05f, 1.0f)
    , _bottomPid(3.0f, 0.04f, 1.2f)
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
    ESP_LOGI(TAG, "Reloaded MachineSettings & PID Library into RAM.");
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
        _topSensor.begin();
        _bottomSensor.begin();
    }

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

    // Priority 7: Synchronous Control Loop (10 Hz / 100ms)
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
