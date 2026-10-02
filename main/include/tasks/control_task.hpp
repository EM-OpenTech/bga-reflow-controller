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
 * @file control_task.hpp
 * @brief Core 1 10 Hz synchronous control loop task.
 *
 * Runs deterministic PID temperature regulation, FSM process step runner,
 * and command queue processing.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================================
// Control Task (Core 1 / 10 Hz)
// ============================================================================

namespace app {

class AppController;

/**
 * @brief Synchronous Control Loop Task running on Core 1 at Priority 7.
 * Cycle: 10 Hz (100 ms) using vTaskDelayUntil.
 * Pipeline: SPI Sensor Read -> FSM Update -> QuickPID Compute -> BurstFire SSR Modulate -> Context Update.
 *
 * @param pvParameters Pointer to parent AppController instance.
 */
void controlTask(void* pvParameters);

} // namespace app

