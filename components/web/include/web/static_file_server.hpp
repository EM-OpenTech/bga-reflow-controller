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
 * @file static_file_server.hpp
 * @brief Flash-embedded and sandboxed HTTP Static File Server.
 *
 * Serves zero-copy gzip-compressed static web assets (HTML, CSS, JS) embedded
 * directly in firmware ROM flash, with MIME type detection and path validation.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_http_server.h"

namespace web {

/**
 * @class StaticFileServer
 * @brief Flash-embedded and sandboxed HTTP Static File Server.
 */
class StaticFileServer {
public:
    /**
     * @brief Handler function for HTTP GET requests targeting static files.
     */
    static esp_err_t fileGetHandler(httpd_req_t *req);

    /**
     * @brief Determine MIME Content-Type string from file extension.
     */
    static const char* getContentType(const char* filepath);
};

} // namespace web
