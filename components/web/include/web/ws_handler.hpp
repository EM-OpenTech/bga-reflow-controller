/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This program is free software: you can redistribute it/or modify
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
 * @file ws_handler.hpp
 * @brief WebSocket telemetry broadcaster and client connection manager.
 *
 * Manages WebSocket client connections on `/ws`, serializes high-frequency 10 Hz
 * live telemetry JSON packets, and distributes them asynchronously across connected clients.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_http_server.h"
#include "fsm/reflow_fsm.hpp"
#include <mutex>
#include <vector>
#include <string>

namespace web {

/// Capture ESP-IDF log line into ring buffer for Web UI
void appendLogLine(const std::string& line);

/// Get copy of latest log lines for Web UI
std::vector<std::string> getLatestLogs(size_t maxCount = 20);

/// Maximum concurrent WebSocket browser connections
constexpr size_t MAX_WS_CLIENTS = 4;

/**
 * @brief Telemetry snapshot structure sent to connected WebSocket clients.
 */
struct TelemetryData {
    uint8_t     stateEnum          = 0;       ///< Process state enumeration value
    std::string stateStr           = "IDLE";  ///< Process state string representation
    bool        preheatDone        = false;   ///< True if bottom preheat phase is finished
    float       topTemp            = 0.0f;    ///< Top heater actual temperature (°C)
    float       bottomTemp         = 0.0f;    ///< Bottom heater actual temperature (°C)
    float       topSet             = 0.0f;    ///< Top heater active setpoint (°C)
    float       bottomSet          = 0.0f;    ///< Bottom heater active setpoint (°C)
    float       topPower           = 0.0f;    ///< Top heater output power percentage (0-100%)
    float       bottomPower        = 0.0f;    ///< Bottom heater output power percentage (0-100%)
    uint32_t    elapsedSec         = 0;       ///< Process elapsed run duration (seconds)
    uint32_t    talSec             = 0;       ///< Accumulated Time-Above-Liquidus duration (seconds)
    bool        fan                = false;   ///< Cooling fan active status
    bool        lamp               = false;   ///< Work lamp active status
    uint8_t     topStep            = 0;       ///< Current profile step index for top channel
    uint8_t     bottomStep         = 0;       ///< Current profile step index for bottom channel
    bool        topSettling        = false;   ///< True if top channel is in settle-gate tolerance window
    bool        bottomSettling     = false;   ///< True if bottom channel is in settle-gate tolerance window
    bool        topHolding         = false;   ///< True if top channel is in active hold phase
    bool        bottomHolding      = false;   ///< True if bottom channel is in active hold phase
    uint32_t    topSettleRemain    = 0;       ///< Remaining settle time for top channel (ms)
    uint32_t    bottomSettleRemain = 0;       ///< Remaining settle time for bottom channel (ms)
    uint32_t    topHoldRemain      = 0;       ///< Remaining hold duration for top channel (ms)
    uint32_t    bottomHoldRemain   = 0;       ///< Remaining hold duration for bottom channel (ms)
    std::string profileFile        = "";      ///< Filename of active reflow profile
    std::vector<fsm::StepMarker> stepMarkers; ///< Step markers recorded during profile execution

    // Active PID Gains
    float       topPidKp           = 0.0f;    ///< Scheduled Kp gain for top PID controller
    float       topPidKi           = 0.0f;    ///< Scheduled Ki gain for top PID controller
    float       topPidKd           = 0.0f;    ///< Scheduled Kd gain for top PID controller
    float       bottomPidKp        = 0.0f;    ///< Scheduled Kp gain for bottom PID controller
    float       bottomPidKi        = 0.0f;    ///< Scheduled Ki gain for bottom PID controller
    float       bottomPidKd        = 0.0f;    ///< Scheduled Kd gain for bottom PID controller

    // Autotuner Telemetry
    bool        autotuneActive     = false;   ///< True if autotuning is currently running
    bool        autotuneFinished   = false;   ///< True if autotuning calculation completed
    bool        autotuneIsTop      = true;    ///< True if tuning top channel, false for bottom
    float       autotuneProgress   = 0.0f;    ///< Autotuner test progress percentage (0-100%)
    float       autotuneTargetTemp = 0.0f;    ///< Autotuner target test temperature (°C)
    float       autotuneKp         = 0.0f;    ///< Calculated autotuner Kp result
    float       autotuneKi         = 0.0f;    ///< Calculated autotuner Ki result
    float       autotuneKd         = 0.0f;    ///< Calculated autotuner Kd result
};

/**
 * @class WebSocketHandler
 * @brief Manages WebSocket client connections and asynchronous broadcasts.
 */
// ============================================================================
// WebSocket Connection & Telemetry Broadcaster
// ============================================================================

class WebSocketHandler {
public:
    explicit WebSocketHandler(httpd_handle_t serverHandle);
    ~WebSocketHandler();

    /**
     * @brief HTTPD WebSocket Request Handler for URI `/ws`.
     */
    static esp_err_t wsHandler(httpd_req_t *req);

    /**
     * @brief Serialize TelemetryData struct into clean, standard-precision JSON string.
     * @param telemetry Telemetry data snapshot.
     * @return Formatted JSON string.
     */
    static std::string serializeTelemetry(const TelemetryData& telemetry);

    /**
     * @brief Broadcast telemetry data JSON frame to all active WebSocket clients.
     * @param telemetry Telemetry data snapshot.
     */
    void broadcast(const TelemetryData& telemetry);

    /**
     * @brief Helper to register WebSocket singleton pointer.
     */
    static void setInstance(WebSocketHandler* instance);
    static WebSocketHandler* getInstance();

    /**
     * @brief Client registration and disconnection management.
     */
    void addClient(int fd);
    void removeClient(int fd);

private:
    httpd_handle_t _serverHandle;                     ///< ESP-IDF HTTP server instance handle
    std::mutex     _clientsMutex;                     ///< Mutex guarding client file descriptor table
    int            _clientFds[MAX_WS_CLIENTS] = {-1, -1, -1, -1}; ///< Connected client socket file descriptors

    static WebSocketHandler* s_instance;              ///< Global singleton instance pointer
};

} // namespace web

