/**
 * @file safety_task.cpp
 * @brief Safety Watchdog Task implementation for Core 1 (Priority 10, 20 Hz / 50ms).
 */

#include "tasks/safety_task.hpp"
#include "app_controller.hpp"
#include "esp_log.h"

static const char* TAG = "SafetyTask";

namespace app {

void safetyTask(void* pvParameters)
{
    auto* app = static_cast<AppController*>(pvParameters);
    ESP_LOGI(TAG, "Safety Task started on Core %d (Priority %d, 20 Hz)",
             xPortGetCoreID(), (int)uxTaskPriorityGet(nullptr));

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(50); // 20 Hz (50 ms)

    while (true) {
        // In simulation mode, skip hardware safety watchdog to allow testing without connected thermocouples
        if (app->getSettings().simulationMode) {
            vTaskDelayUntil(&lastWakeTime, frequency);
            continue;
        }

        // 1. Get latest readings and power levels
        const auto& topReading = app->getTopSensor().getLatest();
        const auto& botReading = app->getBottomSensor().getLatest();
        float topPower = app->getTopPid().getOutput();
        float botPower = app->getBottomPid().getOutput();
        float topSet = app->getFsm().getTopSetpoint();
        float botSet = app->getFsm().getBottomSetpoint();

        // 2. Perform safety checks
        bool faultDetected = app->getSafety().check(topReading, botReading, topPower, botPower, topSet, botSet);

        if (faultDetected) {
            ESP_LOGE(TAG, "Safety fault triggered: %s", app->getSafety().getFaultString());
            // Latch emergency fault in FSM
            app->getFsm().triggerFault(topReading.temperature, botReading.temperature);
        }

        // Wait until next 50ms cycle
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

} // namespace app
