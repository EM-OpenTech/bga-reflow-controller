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
 * @file ota_routes.hpp
 * @brief REST API Route Controller for 1-Click Browser Firmware OTA Updates.
 *
 * Handles HTTP binary stream upload, magic-byte header validation, sequential flash writing,
 * dual-partition boot verification, and delayed graceful controller reboot.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_http_server.h"

namespace web {

/**
 * @class OtaRoutes
 * @brief HTTP route handlers for Over-The-Air firmware upgrades.
 */
class OtaRoutes {
public:
    /**
     * @brief POST /api/ota - Stream-writes uploaded firmware binary into the next OTA partition.
     */
    static esp_err_t postOtaUpdateHandler(httpd_req_t* req);
};

} // namespace web
