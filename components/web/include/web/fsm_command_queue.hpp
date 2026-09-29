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
 * @file fsm_command_queue.hpp
 * @brief Thread-safe FSM command queue for cross-core FSM control.
 *
 * The REST API (Core 0, HTTP handler) MUST NOT call FSM methods directly,
 * because the FSM runs exclusively on Core 1 inside the control task loop.
 * Direct calls from Core 0 cause race conditions on StepRunner fields.
 *
 * Core 0 posts an FsmCommand into this queue, and Core 1 drains it before
 * executing fsm.update() each tick.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "config/machine_config.hpp"
#include <cstdint>

namespace app {

// ============================================================================
// Command types
// ============================================================================
enum class FsmCommandType : uint8_t {
    PREHEAT,        ///< Start preheat – profile ptr carries the loaded profile (caller allocates, Core 1 frees)
    REFLOW,         ///< Transition from PREHEAT_DONE → SOAK
    STOP,           ///< Graceful stop → COOLING
    SKIP_STEP,      ///< Advance current step runner to next step
    RESET_FAULT,    ///< Clear FAULT state → IDLE
    AUTOTUNE_START, ///< Start PID autotune test for specified channel and target temp
    AUTOTUNE_STOP,  ///< Abort PID autotune test -> IDLE
    ENTER_BACKUP,   ///< Enter BACKUP state (lock heating & all controls)
    EXIT_BACKUP,    ///< Exit BACKUP state -> IDLE
};

/**
 * @brief POD-compatible command item for xQueueSend() / xQueueReceive().
 *
 * FreeRTOS queues copy items via memcpy — no C++ constructors are called.
 * Only trivially-copyable types are safe inside this struct.
 *
 * For PREHEAT, a heap-allocated config::ReflowProfile* is carried.
 * Ownership: Core 0 allocates (new), Core 1 frees (delete) after startPreheat().
 * If the queue post fails, Core 0 must delete immediately to avoid leaks.
 *
 * For AUTOTUNE_START, tuneIsTop and tuneTargetTemp are set.
 * All other command types leave profile = nullptr.
 */
struct FsmCommand {
    FsmCommandType          type;               ///< Command action identifier
    config::ReflowProfile*  profile = nullptr;  ///< PREHEAT only. Heap-allocated by Core 0, deleted by Core 1.
    bool                    tuneIsTop = true;   ///< AUTOTUNE_START: true=Top, false=Bottom
    float                   tuneTargetTemp = 150.0f; ///< AUTOTUNE_START: target temperature (°C)
};

// ============================================================================
// Singleton queue handle (created once in app_controller init)
// ============================================================================
extern QueueHandle_t g_fsmCmdQueue;

/**
 * @brief Create the FreeRTOS queue. Call once during AppController::begin().
 */
inline bool fsmCmdQueueInit()
{
    g_fsmCmdQueue = xQueueCreate(4, sizeof(FsmCommand));
    return g_fsmCmdQueue != nullptr;
}

/**
 * @brief Post a command from Core 0 (REST API / input handler).
 *        Non-blocking – returns false if queue is full.
 */
inline bool fsmCmdPost(const FsmCommand& cmd)
{
    if (!g_fsmCmdQueue) return false;
    return xQueueSend(g_fsmCmdQueue, &cmd, 0) == pdTRUE;
}

/**
 * @brief Post a command from an ISR context (if ever needed).
 */
inline bool fsmCmdPostFromISR(const FsmCommand& cmd)
{
    if (!g_fsmCmdQueue) return false;
    BaseType_t woken = pdFALSE;
    bool ok = xQueueSendFromISR(g_fsmCmdQueue, &cmd, &woken) == pdTRUE;
    if (woken) portYIELD_FROM_ISR();
    return ok;
}

} // namespace app
