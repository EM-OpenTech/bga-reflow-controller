#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app {

class AppController;

/**
 * @brief Physical Button and Switch Polling Task running on Core 0 at Priority 6.
 * Cycle: 50 Hz (20 ms) using vTaskDelayUntil.
 * Handles hardware debouncing, short-press, long-press, and toggle switch master overrides.
 */
void inputTask(void* pvParameters);

} // namespace app
