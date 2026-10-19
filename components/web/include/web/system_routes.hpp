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
 * @file system_routes.hpp
 * @brief REST API Route Controller for System Diagnostics, Settings, and Security.
 *
 * Handles status polling, machine settings CRUD, UI theme preferences,
 * PID gain scheduling library, telemetry history streaming, and Wi-Fi security.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_http_server.h"

namespace web {

/**
 * @class SystemRoutes
 * @brief HTTP route handlers for core system diagnostics, settings, and telemetry history.
 */
class SystemRoutes {
public:
    /**
     * @brief GET /api/status - Live controller state, memory, POST report, and limits.
     */
    static esp_err_t getStatusHandler(httpd_req_t* req);

    /**
     * @brief GET /api/settings - Read current machine settings JSON.
     */
    static esp_err_t getSettingsHandler(httpd_req_t* req);

    /**
     * @brief POST /api/settings - Update machine settings and trigger Core 1 reload.
     */
    static esp_err_t postSettingsHandler(httpd_req_t* req);

    /**
     * @brief GET /api/theme - Read UI theme and chart visibility flags.
     */
    static esp_err_t getThemeHandler(httpd_req_t* req);

    /**
     * @brief POST /api/theme - Save UI theme and chart display preferences.
     */
    static esp_err_t postThemeHandler(httpd_req_t* req);

    /**
     * @brief GET /api/pidlibrary - Read PID multi-point interpolation library.
     */
    static esp_err_t getPidLibraryHandler(httpd_req_t* req);

    /**
     * @brief POST /api/pidlibrary - Persist PID interpolation library points.
     */
    static esp_err_t postPidLibraryHandler(httpd_req_t* req);

    /**
     * @brief GET /api/history - Stream chunked history buffer for graph reload.
     */
    static esp_err_t getHistoryHandler(httpd_req_t* req);

    /**
     * @brief GET /api/security/status - Read SoftAP SSID and password status.
     */
    static esp_err_t getSecurityStatusHandler(httpd_req_t* req);

    /**
     * @brief POST /api/security/wifi - Update SoftAP credentials or confirm default.
     */
    static esp_err_t postSecurityWifiHandler(httpd_req_t* req);

    /**
     * @brief POST /api/backup/complete - Mark system backup as executed for this session.
     */
    static esp_err_t postBackupCompleteHandler(httpd_req_t* req);
};

} // namespace web
