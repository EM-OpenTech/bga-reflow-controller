#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "fsm/reflow_fsm.hpp"

namespace app {

/**
 * @brief Thread-Safe Shared System State Context for ESP-IDF v6.0.2.
 * 
 * Provides atomic snapshots of live telemetry and operational state between
 * Core 1 (control_task / safety_task) and Core 0 (web_task / input_task).
 */
struct SystemContextData {
    // Sensor readings (°C)
    float topTemp          = 0.0f;
    float bottomTemp       = 0.0f;

    // FSM State
    fsm::ReflowState state      = fsm::ReflowState::IDLE;
    std::string stateStr        = "IDLE";
    std::string activeProfileFile = "";  ///< Active profile filename – read-only for Core 0
    bool preheatDone            = false;
    uint32_t elapsedSec    = 0;
    uint32_t talSec        = 0;

    // Setpoints & Power Output
    float topSetpoint      = 0.0f;
    float bottomSetpoint   = 0.0f;
    float topPower         = 0.0f;
    float bottomPower      = 0.0f;

    // Hardware Actuators State
    bool fanActive         = false;
    bool lampActive        = false;
    bool ssrTopActive      = false;
    bool ssrBottomActive   = false;

    // Step Execution Progress
    uint8_t topStep        = 0;
    uint8_t bottomStep     = 0;
    bool topSettling       = false;
    bool bottomSettling    = false;
    bool topHolding        = false;
    bool bottomHolding     = false;
    uint32_t topSettleRemain    = 0;
    uint32_t bottomSettleRemain = 0;
    uint32_t topHoldRemain      = 0;
    uint32_t bottomHoldRemain   = 0;

    // Step Markers for Chart Visualisation
    std::vector<fsm::StepMarker> stepMarkers;

    // Active PID Gains
    float    topPidKp            = 0.0f;
    float    topPidKi            = 0.0f;
    float    topPidKd            = 0.0f;
    float    bottomPidKp         = 0.0f;
    float    bottomPidKi         = 0.0f;
    float    bottomPidKd         = 0.0f;

    // Autotuner Telemetry
    bool     autotuneActive      = false;
    bool     autotuneFinished    = false;
    bool     autotuneIsTop       = true;
    float    autotuneProgress    = 0.0f;
    float    autotuneTargetTemp  = 0.0f;
    float    autotuneKp          = 0.0f;
    float    autotuneKi          = 0.0f;
    float    autotuneKd          = 0.0f;

    // Telemetry History Buffer for Web Reloads / Multi-Device Sync
    struct TelemetryHistoryPoint {
        uint16_t timeS;
        float topTemp;
        float bottomTemp;
        float topSet;
        float bottomSet;
    };
    std::vector<TelemetryHistoryPoint> history;
};

class SystemContext {
public:
    SystemContext();
    ~SystemContext();

    // Prevent copying
    SystemContext(const SystemContext&) = delete;
    SystemContext& operator=(const SystemContext&) = delete;

    /**
     * @brief Initialize FreeRTOS Mutex.
     */
    bool begin();

    /**
     * @brief Lock the context for atomic multi-field update.
     */
    bool lock(uint32_t timeoutMs = 20);

    /**
     * @brief Unlock the context.
     */
    void unlock();

    /**
     * @brief Get direct mutable reference to internal data (Call between lock() and unlock()!).
     */
    SystemContextData& getData() { return _data; }

    /**
     * @brief Return a complete atomic snapshot of the live state.
     */
    SystemContextData getSnapshot();

private:
    SystemContextData _data;
    SemaphoreHandle_t _mutex = nullptr;
};

} // namespace app
