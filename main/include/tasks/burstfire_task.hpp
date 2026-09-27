#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app {

class AppController;

/**
 * @brief High-Frequency SSR Burst-Fire Modulation Task running on Core 1 at Priority 8.
 * Cycle: 100 Hz (10 ms) using vTaskDelayUntil.
 * Responsibilities:
 *   - Advances Top and Bottom BurstFire time windows (esp_timer microsecond precision)
 *   - Updates SSR GPIO outputs every 10 ms (1% power resolution for 1000ms window, 50Hz zero-cross aligned)
 */
void burstfireTask(void* pvParameters);

} // namespace app
