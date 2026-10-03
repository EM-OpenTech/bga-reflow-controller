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
 * @file app_controller.hpp
 * @brief Master Coordinator aggregating all system managers and controllers.
 *
 * Owns instances of sensors, actuators, PID controllers, burst-fire SSR modulators,
 * safety watchdog, FSM engine, storage, Wi-Fi, and HTTP/WebSocket servers.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "config/machine_config.hpp"
#include "config/pin_config.hpp"
#include "storage/storage_manager.hpp"
#include "output/output_manager.hpp"
#include "input/input_manager.hpp"
#include "sensor/max31856.hpp"
#include "pid/pid_controller.hpp"
#include "output/burst_fire.hpp"
#include "safety/safety_watchdog.hpp"
#include "fsm/reflow_fsm.hpp"
#include "web/wifi_manager.hpp"
#include "web/web_server.hpp"
#include "web/rest_api.hpp"
#include "system_context.hpp"
#include "driver/spi_master.h"
#include "simulation/thermal_simulator.hpp"

namespace app {

/**
 * @class AppController
 * @brief Master Coordinator aggregating all system managers and controllers.
 */
// ============================================================================
// Master Application Controller
// ============================================================================

class AppController {
public:
    AppController();
    ~AppController();

    // Delete copy operations
    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    /**
     * @brief Initialize all hardware drivers, storage, control loops, and networking in order.
     * @return true on complete success.
     */
    bool begin();

    /**
     * @brief Spawn all 5 Dual-Core FreeRTOS tasks (Core 1: Safety, BurstFire, Control; Core 0: Input, Web).
     */
    void startTasks();

    /**
     * @brief Reload MachineSettings and PID Library from Storage into RAM.
     */
    void reloadSettingsAndPidLibrary();

    // Accessors for Tasks
    SystemContext&           getContext()      { return _context; }
    storage::StorageManager& getStorage()      { return _storage; }
    config::MachineSettings& getSettings()     { return _settings; }
    output::OutputManager&   getOutputs()      { return _outputs; }
    input::InputManager&     getInputs()       { return _inputs; }
    sensor::MAX31856&        getTopSensor()    { return _topSensor; }
    sensor::MAX31856&        getBottomSensor() { return _bottomSensor; }
    sim::ThermalSimulator&   getSimulator()    { return _simulator; }
    pid::PIDController&      getTopPid()       { return _topPid; }
    pid::PIDController&      getBottomPid()    { return _bottomPid; }
    output::BurstFire&       getTopBurst()     { return _topBurst; }
    output::BurstFire&       getBottomBurst()  { return _bottomBurst; }
    safety::SafetyWatchdog&  getSafety()       { return _safety; }
    fsm::ReflowFSM&          getFsm()          { return _fsm; }
    web::WifiManager&        getWifi()         { return _wifi; }
    web::WebServer&          getWebServer()    { return _webServer; }
    const web::TaskHandles&  getTaskHandles() const { return _taskHandles; }

private:
    // Core Shared State & Storage
    SystemContext           _context;     ///< Shared system state and telemetry context
    config::MachineSettings _settings;    ///< Machine settings in RAM
    storage::StorageManager _storage;     ///< LittleFS file storage manager
    web::TaskHandles        _taskHandles; ///< Task handles for runtime stack monitoring

    // Hardware Actuators & Inputs
    output::OutputManager   _outputs;     ///< Actuator GPIO and buzzer output manager
    input::InputManager     _inputs;      ///< Debounced physical button and switch inputs

    // Temperature Sensors (SPI2_HOST)
    sensor::MAX31856        _topSensor;   ///< Top thermocouple SPI hardware driver
    sensor::MAX31856        _bottomSensor;///< Bottom thermocouple SPI hardware driver
    sim::ThermalSimulator   _simulator;   ///< Software thermal physics simulator

    // Control Services
    pid::PIDController      _topPid;      ///< Top heater closed-loop PID controller
    pid::PIDController      _bottomPid;   ///< Bottom heater closed-loop PID controller
    output::BurstFire       _topBurst;    ///< Top SSR burst-fire time-window modulator
    output::BurstFire       _bottomBurst; ///< Bottom SSR burst-fire time-window modulator

    // Safety & Process Engine
    safety::SafetyWatchdog  _safety;      ///< Real-time thermal and hardware safety watchdog
    fsm::ReflowFSM          _fsm;         ///< Dual-channel reflow process state machine

    // Networking & Web
    web::WifiManager        _wifi;        ///< SoftAP Wi-Fi and Captive Portal DNS manager
    web::WebServer          _webServer;   ///< HTTP REST API and WebSocket web server

    bool initSpiBus();
};

} // namespace app

