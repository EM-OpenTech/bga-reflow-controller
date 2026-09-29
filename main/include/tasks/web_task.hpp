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
 * @file web_task.hpp
 * @brief Core 0 2 Hz WebSocket telemetry streaming task.
 *
 * Broadcasts telemetry snapshots from SystemContext to connected browser clients.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app {

class AppController;

/**
 * @brief Live WebSocket Telemetry Streaming Task running on Core 0 at Priority 5.
 * Broadcasts telemetry frames from SystemContext to connected browser clients.
 * Rate: 2 Hz (500 ms) periodic telemetry broadcast.
 *
 * @param pvParameters Pointer to parent AppController instance.
 */
void webTask(void* pvParameters);

} // namespace app

