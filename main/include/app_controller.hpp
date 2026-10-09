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

#include <atomic>
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
#include "esp_system.h"
#include "simulation/thermal_simulator.hpp"

namespace app {

/**
 * @brief Power-On Self-Test (POST) and post-mortem boot diagnosis report.
 */
struct PostReport {
    esp_reset_reason_t resetReason       = ESP_RST_UNKNOWN;
    const char*        resetReasonStr    = "UNKNOWN";
    bool               wasWatchdogReset  = false;  ///< True if previous reboot was Task/Interrupt WDT
    bool               wasBrownoutReset  = false;  ///< True if previous reboot was voltage dip
    size_t             freeHeapBytes     = 0;      ///< Free internal SRAM heap (bytes)
    size_t             freePsramBytes    = 0;      ///< Free Octal PSRAM (bytes)
    bool               littleFsOk        = false;  ///< Flash filesystem mount status
    size_t             littleFsFreeBytes = 0;      ///< Free LittleFS storage space (bytes)
    bool               topSensorOk       = false;  ///< Top MAX31856 SPI probe status
    float              topCjTemp         = 0.0f;   ///< Top cold-junction probe temperature (°C)
    float              topRawTemp        = 0.0f;   ///< Top thermocouple probe temperature (°C)
    bool               bottomSensorOk    = false;  ///< Bottom MAX31856 SPI probe status
    float              bottomCjTemp      = 0.0f;   ///< Bottom cold-junction probe temperature (°C)
    float              bottomRawTemp     = 0.0f;   ///< Bottom thermocouple probe temperature (°C)
    bool               outputsSafe       = false;  ///< GPIO SSR safe-state verified (0V)
    bool               allPassed         = false;  ///< True if all critical POST stages passed
};

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

    // Sensor & Control Snapshot Mailbox (Lock-Free Double Buffer for Core 1)
    struct SensorSnapshot {
        sensor::SensorReading top;
        sensor::SensorReading bottom;
        float topPower       = 0.0f;
        float bottomPower    = 0.0f;
        float topSetpoint    = 0.0f;
        float bottomSetpoint = 0.0f;
    };

    /**
     * @brief Atomically publish new sensor readings and control state from control_task (Single-Writer).
     * @param top Latest Top thermocouple reading.
     * @param bottom Latest Bottom thermocouple reading.
     * @param topPower Top PID output power (0-100%).
     * @param bottomPower Bottom PID output power (0-100%).
     * @param topSet Top target setpoint (°C).
     * @param bottomSet Bottom target setpoint (°C).
     */
    void publishSensorSnapshot(const sensor::SensorReading& top,
                               const sensor::SensorReading& bottom,
                               float topPower = 0.0f, float bottomPower = 0.0f,
                               float topSet = 0.0f, float bottomSet = 0.0f) {
        uint8_t nextIdx = 1 - _snapshotIndex.load(std::memory_order_relaxed);
        _sensorSnapshots[nextIdx].top            = top;
        _sensorSnapshots[nextIdx].bottom         = bottom;
        _sensorSnapshots[nextIdx].topPower       = topPower;
        _sensorSnapshots[nextIdx].bottomPower    = bottomPower;
        _sensorSnapshots[nextIdx].topSetpoint    = topSet;
        _sensorSnapshots[nextIdx].bottomSetpoint = bottomSet;
        _snapshotIndex.store(nextIdx, std::memory_order_release);
    }

    /**
     * @brief Atomically retrieve latest complete snapshot without blocking or locks.
     * @param snapshot Output reference for the full synchronized snapshot.
     */
    void getSensorSnapshot(SensorSnapshot& snapshot) const {
        uint8_t currIdx = _snapshotIndex.load(std::memory_order_acquire);
        snapshot = _sensorSnapshots[currIdx];
    }

    /**
     * @brief Backward-compatible overload to retrieve just sensor readings.
     * @param top Output reference for Top reading.
     * @param bottom Output reference for Bottom reading.
     */
    void getSensorSnapshot(sensor::SensorReading& top, sensor::SensorReading& bottom) const {
        uint8_t currIdx = _snapshotIndex.load(std::memory_order_acquire);
        top    = _sensorSnapshots[currIdx].top;
        bottom = _sensorSnapshots[currIdx].bottom;
    }

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
    const PostReport&        getPostReport() const  { return _postReport; }

private:
    // Core Shared State & Storage
    SystemContext           _context;     ///< Shared system state and telemetry context
    config::MachineSettings _settings;    ///< Machine settings in RAM
    storage::StorageManager _storage;     ///< LittleFS file storage manager
    web::TaskHandles        _taskHandles; ///< Task handles for runtime stack monitoring
    PostReport              _postReport;  ///< Cached boot POST report

    // Sensor Double-Buffer Mailbox (Lock-Free)
    SensorSnapshot          _sensorSnapshots[2];
    std::atomic<uint8_t>    _snapshotIndex{0};

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
    PostReport runPowerOnSelfTest();
};

} // namespace app

