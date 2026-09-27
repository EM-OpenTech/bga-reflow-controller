/**
 * @file fsm_command_queue.cpp
 * @brief Definition of the global FSM command queue handle.
 */
#include "web/fsm_command_queue.hpp"

namespace app {
    QueueHandle_t g_fsmCmdQueue = nullptr;
}
