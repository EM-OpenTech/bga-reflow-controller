#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app {

class AppController;

/**
 * @brief Live WebSocket Telemetry Streaming Task running on Core 0 at Priority 5.
 * Broadcasts telemetry frames from SystemContext to connected browser clients.
 * Rate: 2 Hz (500 ms) periodic telemetry broadcast.
 */
void webTask(void* pvParameters);

} // namespace app
