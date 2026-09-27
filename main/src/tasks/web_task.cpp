/**
 * @file web_task.cpp
 * @brief Live WebSocket Telemetry Streaming Task implementation for Core 0 (Priority 5, 2 Hz / 1 Hz).
 */

#include "tasks/web_task.hpp"
#include "app_controller.hpp"
#include "esp_log.h"

static const char* TAG = "WebTask";

namespace app {

void webTask(void* pvParameters)
{
    auto* app = static_cast<AppController*>(pvParameters);
    ESP_LOGI(TAG, "Web Telemetry Task started on Core %d (Priority %d)",
             xPortGetCoreID(), (int)uxTaskPriorityGet(nullptr));

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(500); // 2 Hz (500 ms)

    while (true) {
        // 1. Take atomic snapshot of live SystemContext
        SystemContextData snapshot = app->getContext().getSnapshot();

        // 2. Map snapshot to WebServer TelemetryData struct
        web::TelemetryData t;
        t.stateEnum      = static_cast<uint8_t>(snapshot.state);
        t.stateStr       = snapshot.stateStr;
        t.preheatDone    = snapshot.preheatDone;
        t.topTemp        = snapshot.topTemp;
        t.bottomTemp     = snapshot.bottomTemp;
        t.topSet         = snapshot.topSetpoint;
        t.bottomSet      = snapshot.bottomSetpoint;
        t.topPower       = snapshot.topPower;
        t.bottomPower    = snapshot.bottomPower;
        t.elapsedSec     = snapshot.elapsedSec;
        t.talSec         = snapshot.talSec;
        t.fan            = snapshot.fanActive;
        t.lamp           = snapshot.lampActive;
        t.topStep        = snapshot.topStep;
        t.bottomStep     = snapshot.bottomStep;
        t.topSettling    = snapshot.topSettling;
        t.bottomSettling = snapshot.bottomSettling;
        t.topHolding     = snapshot.topHolding;
        t.bottomHolding  = snapshot.bottomHolding;
        t.topSettleRemain    = snapshot.topSettleRemain;
        t.bottomSettleRemain = snapshot.bottomSettleRemain;
        t.topHoldRemain      = snapshot.topHoldRemain;
        t.bottomHoldRemain   = snapshot.bottomHoldRemain;
        t.profileFile    = snapshot.activeProfileFile;
        t.stepMarkers    = snapshot.stepMarkers;
        t.topPidKp       = snapshot.topPidKp;
        t.topPidKi       = snapshot.topPidKi;
        t.topPidKd       = snapshot.topPidKd;
        t.bottomPidKp    = snapshot.bottomPidKp;
        t.bottomPidKi    = snapshot.bottomPidKi;
        t.bottomPidKd    = snapshot.bottomPidKd;
        t.autotuneActive = snapshot.autotuneActive;
        t.autotuneFinished = snapshot.autotuneFinished;
        t.autotuneIsTop  = snapshot.autotuneIsTop;
        t.autotuneProgress = snapshot.autotuneProgress;
        t.autotuneTargetTemp = snapshot.autotuneTargetTemp;
        t.autotuneKp     = snapshot.autotuneKp;
        t.autotuneKi     = snapshot.autotuneKi;
        t.autotuneKd     = snapshot.autotuneKd;

        // 3. Broadcast to all active WebSocket clients
        app->getWebServer().broadcastTelemetry(t);

        // Wait until next 500ms cycle
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

} // namespace app
