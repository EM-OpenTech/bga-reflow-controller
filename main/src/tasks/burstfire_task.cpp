/**
 * @file burstfire_task.cpp
 * @brief High-Frequency SSR Burst-Fire Modulation Task implementation for Core 1 (Priority 8, 100 Hz / 10ms).
 */

#include "tasks/burstfire_task.hpp"
#include "app_controller.hpp"
#include "esp_log.h"

static const char* TAG = "BurstFireTask";

namespace app {

void burstfireTask(void* pvParameters)
{
    auto* app = static_cast<AppController*>(pvParameters);
    ESP_LOGI(TAG, "BurstFire Task started on Core %d (Priority %d, 100 Hz / 10ms)",
             xPortGetCoreID(), (int)uxTaskPriorityGet(nullptr));

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(10); // 100 Hz (10 ms)

    while (true) {
        // 1. Advance Burst-Fire PWM Windows
        app->getTopBurst().update();
        app->getBottomBurst().update();

        // 2. Drive Solid State Relay Hardware Outputs
        bool topSsrOn = app->getTopBurst().getState();
        bool botSsrOn = app->getBottomBurst().getState();

        app->getOutputs().setSsrTop(topSsrOn);
        app->getOutputs().setSsrBottom(botSsrOn);

        // 3. Wait for Next 10 ms Tick with Zero Cumulative Drift
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

} // namespace app
