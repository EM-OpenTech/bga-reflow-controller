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
 * @brief Master Coordinator aggregating all system managers and controllers.
 */
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
     * @brief Spawn all 4 Dual-Core FreeRTOS tasks.
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
    SystemContext           _context;
    config::MachineSettings _settings;
    storage::StorageManager _storage;
    web::TaskHandles        _taskHandles;

    // Hardware Actuators & Inputs
    output::OutputManager   _outputs;
    input::InputManager     _inputs;

    // Temperature Sensors (SPI2_HOST)
    sensor::MAX31856        _topSensor;
    sensor::MAX31856        _bottomSensor;
    sim::ThermalSimulator   _simulator;

    // Control Services
    pid::PIDController      _topPid;
    pid::PIDController      _bottomPid;
    output::BurstFire       _topBurst;
    output::BurstFire       _bottomBurst;

    // Safety & Process Engine
    safety::SafetyWatchdog  _safety;
    fsm::ReflowFSM          _fsm;

    // Networking & Web
    web::WifiManager        _wifi;
    web::WebServer          _webServer;

    bool initSpiBus();
};

} // namespace app
