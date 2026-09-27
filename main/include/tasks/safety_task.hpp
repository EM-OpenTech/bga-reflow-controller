#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app {

class AppController;

/**
 * @brief Safety Watchdog Task running on Core 1 at Priority 10 (Highest).
 * Cycle: 20 Hz (50 ms) using vTaskDelayUntil.
 * Evaluates overtemp, IC hardware faults, sensor wire breaks, stuck SSRs, and no-rise failure.
 */
void safetyTask(void* pvParameters);

} // namespace app
