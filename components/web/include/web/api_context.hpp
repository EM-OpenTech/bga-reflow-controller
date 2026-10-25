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
 * @file api_context.hpp
 * @brief Shared context and helper utilities for REST API route controllers.
 *
 * Provides access to injected subsystems, FreeRTOS task handles,
 * and POST diagnostics across modularized route handlers.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fsm/reflow_fsm.hpp"
#include "storage/storage_manager.hpp"
#include "web/wifi_manager.hpp"
#include <cmath>
#include <string>

namespace app {
class SystemContext;
}

namespace web {

/**
 * @brief FreeRTOS Task Handles for runtime stack & health monitoring.
 */
struct TaskHandles {
    TaskHandle_t safetyTask = nullptr;    ///< Task handle for safety watchdog loop
    TaskHandle_t burstfireTask = nullptr; ///< Task handle for zero-cross burst-fire SSR loop
    TaskHandle_t controlTask = nullptr;   ///< Task handle for Core 1 5Hz (200ms) PID control loop
    TaskHandle_t inputTask = nullptr;     ///< Task handle for button debouncing loop
    TaskHandle_t webTask = nullptr;       ///< Task handle for HTTP/WebSocket server
};

#include "config/post_report.hpp"

/**
 * @brief Boot Post-Mortem and POST diagnostics data model for REST API status.
 * Re-uses the canonical definition from config::PostReport (Single Source of Truth).
 */
using PostReportInfo = config::PostReport;

/**
 * @class ApiContext
 * @brief Singleton registry of subsystem pointers and HTTP utility methods.
 */
class ApiContext {
public:
    static ApiContext& instance() {
        static ApiContext s_instance;
        return s_instance;
    }

    storage::StorageManager* storage = nullptr;  ///< LittleFS storage manager
    config::MachineSettings* settings = nullptr; ///< Active machine settings
    fsm::ReflowFSM* fsm = nullptr;               ///< Reflow finite state machine
    WifiManager* wifi = nullptr;                 ///< Wi-Fi network manager
    app::SystemContext* context = nullptr;       ///< Thread-safe system snapshot
    bool backupTaken = false;                    ///< System backup completed flag
    TaskHandles taskHandles = {};                ///< Captured task handles
    config::PostReport postReport = {};          ///< Boot POST report diagnostics

    /**
     * @brief Read full HTTP request payload up to MAX_JSON_PAYLOAD_BYTES.
     * @param req Incoming HTTP request pointer.
     * @return Payload string or empty string on error/overflow.
     */
    static std::string readRequestBody(httpd_req_t* req);

    /**
     * @brief Send standard JSON success response {"success":true}.
     */
    static esp_err_t sendJsonSuccess(httpd_req_t* req, const char* extraFields = nullptr);

    /**
     * @brief Math helper: round float/double to 1 decimal place.
     */
    static double round1(double v) { return std::round(v * 10.0) / 10.0; }

    /**
     * @brief Math helper: round float/double to 2 decimal places.
     */
    static double round2(double v) { return std::round(v * 100.0) / 100.0; }

    /**
     * @brief Math helper: round float/double to 3 decimal places.
     */
    static double round3(double v) { return std::round(v * 1000.0) / 1000.0; }

private:
    ApiContext() = default;
};

} // namespace web
