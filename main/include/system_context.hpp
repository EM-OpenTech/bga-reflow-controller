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
 * @file system_context.hpp
 * @brief Thread-safe shared system state context across dual ESP32-S3 cores.
 *
 * Provides atomic snapshots of live telemetry and operational state between
 * Core 1 (control_task / safety_task) and Core 0 (web_task / input_task).
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "fsm/reflow_fsm.hpp"

namespace app {

/**
 * @brief Thread-safe shared system state snapshot data model.
 */
struct SystemContextData {
    // Sensor readings (°C)
    float       topTemp            = 0.0f;    ///< Measured top heater temperature (°C)
    float       bottomTemp         = 0.0f;    ///< Measured bottom heater temperature (°C)

    // FSM State
    fsm::ReflowState state         = fsm::ReflowState::IDLE; ///< Active process state
    std::string stateStr           = "IDLE";  ///< Active process state string representation
    std::string activeProfileFile  = "";      ///< Active profile filename – read-only for Core 0
    bool        preheatDone        = false;   ///< Preheat completion indicator
    uint32_t    elapsedSec         = 0;       ///< Process elapsed time (seconds)
    uint32_t    talSec             = 0;       ///< Time Above Liquidus duration (seconds)

    // Setpoints & Power Output
    float       topSetpoint        = 0.0f;    ///< Active top heater setpoint (°C)
    float       bottomSetpoint     = 0.0f;    ///< Active bottom heater setpoint (°C)
    float       topPower           = 0.0f;    ///< Modulated top heater power (0-100%)
    float       bottomPower        = 0.0f;    ///< Modulated bottom heater power (0-100%)

    // Hardware Actuators State
    bool        fanActive          = false;   ///< Cooling fan active status
    bool        lampActive         = false;   ///< Work lamp active status
    bool        ssrTopActive       = false;   ///< Top SSR gate status
    bool        ssrBottomActive    = false;   ///< Bottom SSR gate status

    // Step Execution Progress
    uint8_t     topStep            = 0;       ///< Current top step index
    uint8_t     bottomStep         = 0;       ///< Current bottom step index
    bool        topSettling        = false;   ///< Top settle-gate active flag
    bool        bottomSettling     = false;   ///< Bottom settle-gate active flag
    bool        topHolding         = false;   ///< Top hold phase active flag
    bool        bottomHolding      = false;   ///< Bottom hold phase active flag
    uint32_t    topSettleRemain    = 0;       ///< Remaining top settle time (ms)
    uint32_t    bottomSettleRemain = 0;       ///< Remaining bottom settle time (ms)
    uint32_t    topHoldRemain      = 0;       ///< Remaining top hold time (ms)
    uint32_t    bottomHoldRemain   = 0;       ///< Remaining bottom hold time (ms)

    // Step Markers for Chart Visualisation
    std::vector<fsm::StepMarker> stepMarkers; ///< Step marker points for frontend chart

    // Active PID Gains
    float       topPidKp           = 0.0f;    ///< Top PID proportional gain
    float       topPidKi           = 0.0f;    ///< Top PID integral gain
    float       topPidKd           = 0.0f;    ///< Top PID derivative gain
    float       bottomPidKp        = 0.0f;    ///< Bottom PID proportional gain
    float       bottomPidKi        = 0.0f;    ///< Bottom PID integral gain
    float       bottomPidKd        = 0.0f;    ///< Bottom PID derivative gain

    // Autotuner Telemetry
    bool        autotuneActive     = false;   ///< Autotuning in progress
    bool        autotuneFinished   = false;   ///< Autotuning finished
    bool        autotuneIsTop      = true;    ///< Autotuning top channel flag
    float       autotuneProgress   = 0.0f;    ///< Autotuning progress (0-100%)
    float       autotuneTargetTemp = 0.0f;    ///< Autotuning target temperature (°C)
    float       autotuneKp         = 0.0f;    ///< Tuned Kp result
    float       autotuneKi         = 0.0f;    ///< Tuned Ki result
    float       autotuneKd         = 0.0f;    ///< Tuned Kd result

    // Telemetry History Buffer for Web Reloads / Multi-Device Sync
    struct TelemetryHistoryPoint {
        uint16_t timeS;      ///< Timestamp in seconds
        float    topTemp;    ///< Top temperature (°C)
        float    bottomTemp; ///< Bottom temperature (°C)
        float    topSet;     ///< Top setpoint (°C)
        float    bottomSet;  ///< Bottom setpoint (°C)
    };
    std::vector<TelemetryHistoryPoint> history; ///< Rolling history point buffer
};

/**
 * @class SystemContext
 * @brief Thread-safe shared system state context with mutex synchronisation.
 */
// ============================================================================
// Thread-Safe Inter-Core Shared System Context
// ============================================================================

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
    SystemContextData _data;             ///< Shared state data container
    SemaphoreHandle_t _mutex = nullptr;  ///< FreeRTOS mutex synchronizing cross-core access
};

} // namespace app

