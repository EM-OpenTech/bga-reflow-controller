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
 * @file control_routes.hpp
 * @brief REST API Route Controller for Process Control & Hardware Overrides.
 *
 * Dispatches FSM lifecycle commands (preheat, reflow, stop, autotune, skip, resetFault, backup)
 * and manual hardware overrides (fan, lamp) with fail-safe state validation.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_http_server.h"

namespace web {

/**
 * @class ControlRoutes
 * @brief HTTP route handlers for process execution and peripheral overrides.
 */
class ControlRoutes {
public:
    /**
     * @brief POST /api/control - Process state execution command dispatcher.
     */
    static esp_err_t postControlHandler(httpd_req_t* req);

    /**
     * @brief POST /api/overrides - Direct fan & lamp hardware override controls.
     */
    static esp_err_t postOverridesHandler(httpd_req_t* req);
};

} // namespace web
