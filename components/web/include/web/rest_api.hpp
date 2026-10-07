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
 * @file rest_api.hpp
 * @brief REST API Dispatcher and Route Handlers for BGA Reflow Controller.
 *
 * Handles HTTP GET/POST/DELETE routes for system status, machine settings,
 * profile CRUD operations, manual overrides, history logs, PID tuning libraries,
 * Wi-Fi credentials, and OTA updates.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage/storage_manager.hpp"
#include "fsm/reflow_fsm.hpp"
#include "web/wifi_manager.hpp"

// Forward declaration – full type available via system_context.hpp in main/include
namespace app { class SystemContext; }

namespace web {

/**
 * @brief FreeRTOS Task Handles for runtime stack & health monitoring.
 */
struct TaskHandles {
    TaskHandle_t safetyTask    = nullptr; ///< Task handle for safety watchdog loop
    TaskHandle_t burstfireTask = nullptr; ///< Task handle for zero-cross burst-fire SSR loop
    TaskHandle_t controlTask   = nullptr; ///< Task handle for Core 1 5Hz (200ms) PID control loop
    TaskHandle_t inputTask     = nullptr; ///< Task handle for button debouncing loop
    TaskHandle_t webTask       = nullptr; ///< Task handle for HTTP/WebSocket server
};

/**
 * @brief Boot Post-Mortem and POST diagnostics data model for REST API status.
 */
struct PostReportInfo {
    int         resetReason       = 0;
    const char* resetReasonStr    = "UNKNOWN";
    bool        wasWatchdogReset  = false;
    bool        wasBrownoutReset  = false;
    size_t      freeHeapBytes     = 0;
    size_t      freePsramBytes    = 0;
    bool        littleFsOk        = false;
    size_t      littleFsFreeBytes = 0;
    bool        topSensorOk       = false;
    float       topCjTemp         = 0.0f;
    float       topRawTemp        = 0.0f;
    bool        bottomSensorOk    = false;
    float       bottomCjTemp      = 0.0f;
    float       bottomRawTemp     = 0.0f;
    bool        outputsSafe       = false;
    bool        allPassed         = false;
};

/**
 * @class RestApi
 * @brief REST API Dispatcher and Route Handlers for Reflow Controller.
 */
class RestApi {
public:
    /**
     * @brief Initialise static dependencies. Call once before httpd_start().
     * @param context  Thread-safe SystemContext – used to read activeProfileFile on Core 0.
     */
    static void init(storage::StorageManager* storage,
                     config::MachineSettings* settings,
                     fsm::ReflowFSM*         fsm,
                     WifiManager*            wifi,
                     app::SystemContext*     context);

    /**
     * @brief Register running FreeRTOS task handles for stack high-watermark diagnostics.
     */
    static void setTaskHandles(const TaskHandles& handles);

    /**
     * @brief Register boot Power-On Self-Test (POST) report for diagnostic status queries.
     */
    static void setPostReport(const PostReportInfo& postInfo);

    // ========================================================================
    // REST ROUTE HANDLERS
    // ========================================================================
    static esp_err_t getStatusHandler(httpd_req_t *req);
    static esp_err_t getSettingsHandler(httpd_req_t *req);
    static esp_err_t postSettingsHandler(httpd_req_t *req);
    static esp_err_t getProfilesHandler(httpd_req_t *req);
    static esp_err_t getProfileHandler(httpd_req_t *req);
    static esp_err_t postProfileHandler(httpd_req_t *req);
    static esp_err_t deleteProfileHandler(httpd_req_t *req);
    static esp_err_t postControlHandler(httpd_req_t *req);
    static esp_err_t postOverridesHandler(httpd_req_t *req);
    static esp_err_t getHistoryHandler(httpd_req_t *req);
    static esp_err_t getSecurityStatusHandler(httpd_req_t *req);
    static esp_err_t postSecurityWifiHandler(httpd_req_t *req);
    // Theme (thin wrapper — theme lives inside settings.json)
    static esp_err_t getThemeHandler(httpd_req_t *req);
    static esp_err_t postThemeHandler(httpd_req_t *req);
    // PID Library (/littlefs/config/pid_library.json)
    static esp_err_t getPidLibraryHandler(httpd_req_t *req);
    static esp_err_t postPidLibraryHandler(httpd_req_t *req);
    // Backup State & Completion Tracking
    static esp_err_t postBackupCompleteHandler(httpd_req_t *req);
    // 1-Click Web OTA Firmware Update
    static esp_err_t postOtaUpdateHandler(httpd_req_t *req);

    // URI / Path parameter parsing helper
    static std::string extractProfileFilenameFromUri(const std::string& uri, const std::string& query = "");

private:
    static storage::StorageManager* s_storage;     ///< Injected pointer to LittleFS storage manager
    static config::MachineSettings* s_settings;    ///< Injected pointer to active machine settings
    static fsm::ReflowFSM*          s_fsm;         ///< Injected pointer to reflow finite state machine
    static WifiManager*             s_wifi;        ///< Injected pointer to Wi-Fi manager
    static app::SystemContext*      s_context;     ///< For safe Core-0 reads of profile name
    static bool                     s_backupTaken; ///< Instance runtime tracking (resets to false on reboot)
    static TaskHandles              s_taskHandles; ///< Captured task handles for stack monitoring
    static PostReportInfo           s_postReport;  ///< Boot POST report diagnostics

    static std::string readRequestBody(httpd_req_t *req);
};

} // namespace web
