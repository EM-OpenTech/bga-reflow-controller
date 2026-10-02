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
 * @file web_server.hpp
 * @brief Main HTTP and WebSocket WebServer Component.
 *
 * Coordinates HTTP server initialization, static route dispatching,
 * REST API registration, and WebSocket telemetry broadcasts.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "storage/storage_manager.hpp"
#include "fsm/reflow_fsm.hpp"
#include "web/wifi_manager.hpp"
#include "web/ws_handler.hpp"
#include <memory>

// Forward declaration – full type available via system_context.hpp in main/include
namespace app { class SystemContext; }

namespace web {

/**
 * @class WebServer
 * @brief Main HTTP and WebSocket WebServer Component.
 */
// ============================================================================
// HTTP & WebSocket WebServer Manager
// ============================================================================

class WebServer {
public:
    WebServer(storage::StorageManager& storage,
              config::MachineSettings& settings,
              fsm::ReflowFSM&         fsm,
              WifiManager&            wifi,
              app::SystemContext&     context);
    ~WebServer();

    /**
     * @brief Start HTTPD server and register all static file, REST, and WebSocket handlers.
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t begin();

    /**
     * @brief Stop the running HTTPD server.
     */
    void stop();

    /**
     * @brief Broadcast telemetry data frame to connected WebSocket clients.
     */
    void broadcastTelemetry(const TelemetryData& telemetry);

private:
    storage::StorageManager& _storage; ///< Reference to LittleFS storage manager
    config::MachineSettings& _settings;///< Reference to machine settings
    fsm::ReflowFSM&         _fsm;      ///< Reference to reflow FSM
    WifiManager&            _wifi;     ///< Reference to Wi-Fi manager
    app::SystemContext&     _context;  ///< Thread-safe SystemContext for Core-0 reads

    httpd_handle_t                    _serverHandle = nullptr; ///< ESP-IDF HTTPD instance handle
    std::unique_ptr<WebSocketHandler> _wsHandler;              ///< WebSocket telemetry broadcast handler

    void registerRoutes();
};

} // namespace web

