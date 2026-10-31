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
 * @brief REST API Dispatcher and Facade for BGA Reflow Controller.
 *
 * Coordinates lifecycle initialization and provides routing endpoints delegated
 * to SystemRoutes, ProfileRoutes, ControlRoutes, and OtaRoutes.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_http_server.h"
#include "web/api_context.hpp"
#include "web/control_routes.hpp"
#include "web/ota_routes.hpp"
#include "web/profile_routes.hpp"
#include "web/system_routes.hpp"
#include <string>

namespace web {

/**
 * @class RestApi
 * @brief REST API Facade delegating to modular route controllers.
 */
class RestApi {
public:
    /**
     * @brief Initialise static dependencies. Call once before httpd_start().
     */
    static void init(storage::StorageManager* storage, config::MachineSettings* settings, fsm::ReflowFSM* fsm,
                     WifiManager* wifi, app::SystemContext* context);

    /**
     * @brief Register running FreeRTOS task handles for stack high-watermark diagnostics.
     */
    static void setTaskHandles(const TaskHandles& handles);

    /**
     * @brief Register boot Power-On Self-Test (POST) report for diagnostic status queries.
     */
    static void setPostReport(const PostReportInfo& postInfo);

    // ========================================================================
    // REST ROUTE HANDLERS (Delegated to Controller Modules)
    // ========================================================================
    static esp_err_t getStatusHandler(httpd_req_t* req) { return SystemRoutes::getStatusHandler(req); }
    static esp_err_t getSettingsHandler(httpd_req_t* req) { return SystemRoutes::getSettingsHandler(req); }
    static esp_err_t postSettingsHandler(httpd_req_t* req) { return SystemRoutes::postSettingsHandler(req); }
    static esp_err_t getProfilesHandler(httpd_req_t* req) { return ProfileRoutes::getProfilesHandler(req); }
    static esp_err_t getProfileHandler(httpd_req_t* req) { return ProfileRoutes::getProfileHandler(req); }
    static esp_err_t postProfileHandler(httpd_req_t* req) { return ProfileRoutes::postProfileHandler(req); }
    static esp_err_t deleteProfileHandler(httpd_req_t* req) { return ProfileRoutes::deleteProfileHandler(req); }
    static esp_err_t postControlHandler(httpd_req_t* req) { return ControlRoutes::postControlHandler(req); }
    static esp_err_t postOverridesHandler(httpd_req_t* req) { return ControlRoutes::postOverridesHandler(req); }
    static esp_err_t getHistoryHandler(httpd_req_t* req) { return SystemRoutes::getHistoryHandler(req); }
    static esp_err_t getSecurityStatusHandler(httpd_req_t* req) { return SystemRoutes::getSecurityStatusHandler(req); }
    static esp_err_t postSecurityWifiHandler(httpd_req_t* req) { return SystemRoutes::postSecurityWifiHandler(req); }
    static esp_err_t getThemeHandler(httpd_req_t* req) { return SystemRoutes::getThemeHandler(req); }
    static esp_err_t postThemeHandler(httpd_req_t* req) { return SystemRoutes::postThemeHandler(req); }
    static esp_err_t getPidLibraryHandler(httpd_req_t* req) { return SystemRoutes::getPidLibraryHandler(req); }
    static esp_err_t postPidLibraryHandler(httpd_req_t* req) { return SystemRoutes::postPidLibraryHandler(req); }
    static esp_err_t postBackupCompleteHandler(httpd_req_t* req) {
        return SystemRoutes::postBackupCompleteHandler(req);
    }
    static esp_err_t postOtaUpdateHandler(httpd_req_t* req) { return OtaRoutes::postOtaUpdateHandler(req); }

    /**
     * @brief URI / Path parameter parsing helper (delegates to ProfileRoutes).
     */
    [[nodiscard]] static std::string extractProfileFilenameFromUri(const std::string& uri,
                                                                   const std::string& query = "") {
        return ProfileRoutes::extractProfileFilenameFromUri(uri, query);
    }
};

} // namespace web
