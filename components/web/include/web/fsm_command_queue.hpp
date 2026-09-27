/**
 * @file fsm_command_queue.hpp
 * @brief Thread-safe FSM command queue for cross-core FSM control.
 *
 * The REST API (Core 0, HTTP handler) MUST NOT call FSM methods directly,
 * because the FSM runs exclusively on Core 1 inside the control task loop.
 * Direct calls from Core 0 cause race conditions on StepRunner fields,
 * leading to stale setpoints after skipStep() and other corruption.
 *
 * Solution: Core 0 posts a FsmCommand into this queue.
 *           Core 1 drains the queue BEFORE calling fsm.update() each tick.
 *
 * Queue depth of 4 is sufficient – commands are user-initiated one at a time.
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "config/machine_config.hpp"
#include <cstdint>

namespace app {

// ---------------------------------------------------------------------------
// Command types
// ---------------------------------------------------------------------------
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
    FsmCommandType          type;
    config::ReflowProfile*  profile = nullptr;  ///< PREHEAT only. Heap-allocated by Core 0, deleted by Core 1.
    bool                    tuneIsTop = true;   ///< AUTOTUNE_START: true=Top, false=Bottom
    float                   tuneTargetTemp = 150.0f; ///< AUTOTUNE_START: target temperature (°C)
};

// ---------------------------------------------------------------------------
// Singleton queue handle (created once in app_controller init)
// ---------------------------------------------------------------------------
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
