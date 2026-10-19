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
 * @file profile_routes.hpp
 * @brief REST API Route Controller for Profile Management & CRUD Operations.
 *
 * Handles listing, retrieving, saving, validating, and deleting reflow profiles
 * stored on the LittleFS partition with schema validation and factory profile protection.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_http_server.h"
#include <string>

namespace web {

/**
 * @class ProfileRoutes
 * @brief HTTP route handlers for profile listing, reading, creation, and deletion.
 */
class ProfileRoutes {
public:
    /**
     * @brief GET /api/profiles & GET /api/profiles/{filename}
     */
    static esp_err_t getProfilesHandler(httpd_req_t* req);

    /**
     * @brief GET /api/profile (Legacy query-param fallback)
     */
    static esp_err_t getProfileHandler(httpd_req_t* req);

    /**
     * @brief POST /api/profiles & POST /api/profile
     */
    static esp_err_t postProfileHandler(httpd_req_t* req);

    /**
     * @brief DELETE /api/profiles/{filename} & DELETE /api/profile?file=XYZ
     */
    static esp_err_t deleteProfileHandler(httpd_req_t* req);

    /**
     * @brief Extract and URL-decode profile filename from URI or query params.
     */
    static std::string extractProfileFilenameFromUri(const std::string& uriStr, const std::string& queryStr = "");

    /**
     * @brief Extract profile filename from an active HTTP request.
     */
    static std::string extractProfileFilename(httpd_req_t* req);
};

} // namespace web
