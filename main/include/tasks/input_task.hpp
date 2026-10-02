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
 * @file input_task.hpp
 * @brief Core 0 50 Hz physical button and switch polling task.
 *
 * Handles hardware debouncing, short-press, long-press, and toggle switch master overrides.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================================
// Hardware Input Polling Task (Core 0 / 50 Hz)
// ============================================================================

namespace app {

class AppController;

/**
 * @brief Physical Button and Switch Polling Task running on Core 0 at Priority 6.
 * Cycle: 50 Hz (20 ms) using vTaskDelayUntil.
 *
 * @param pvParameters Pointer to parent AppController instance.
 */
void inputTask(void* pvParameters);

} // namespace app

