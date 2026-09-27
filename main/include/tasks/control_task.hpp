#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app {

class AppController;

/**
 * @brief Synchronous Control Loop Task running on Core 1 at Priority 7.
 * Cycle: 10 Hz (100 ms) using vTaskDelayUntil.
 * Pipeline: SPI Sensor Read -> FSM Update -> QuickPID Compute -> BurstFire SSR Modulate -> Context Update.
 */
void controlTask(void* pvParameters);

} // namespace app
