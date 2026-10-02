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
 * @file burstfire_task.hpp
 * @brief Core 1 100 Hz SSR zero-crossing burst-fire modulation task.
 *
 * Advances Top and Bottom BurstFire time windows and updates SSR GPIO outputs.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================================
// Burst-Fire Modulation Task (Core 1 / 100 Hz)
// ============================================================================

namespace app {

class AppController;

/**
 * @brief High-Frequency SSR Burst-Fire Modulation Task running on Core 1 at Priority 8.
 * Cycle: 100 Hz (10 ms) using vTaskDelayUntil.
 *
 * @param pvParameters Pointer to parent AppController instance.
 */
void burstfireTask(void* pvParameters);

} // namespace app

