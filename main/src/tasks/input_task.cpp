/**
 * @file input_task.cpp
 * @brief Physical Button and Switch Polling Task implementation for Core 0 (Priority 6, 50 Hz / 20ms).
 */

#include "tasks/input_task.hpp"
#include "app_controller.hpp"
#include "web/fsm_command_queue.hpp"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "InputTask";

namespace app {

void inputTask(void* pvParameters)
{
    auto* app = static_cast<AppController*>(pvParameters);
    ESP_LOGI(TAG, "Input Task started on Core %d (Priority %d, 50 Hz)",
             xPortGetCoreID(), (int)uxTaskPriorityGet(nullptr));

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(20); // 50 Hz (20 ms)

    // Button press duration trackers
    uint64_t startPressBeginUs = 0;
    uint64_t stopPressBeginUs  = 0;
    bool lastStartHeld = false;
    bool lastStopHeld  = false;

    // Toggle switch callbacks setup
    app->getInputs().setOnFanSwitchChanged([app](bool active) {
        ESP_LOGI(TAG, "Hardware SW_FAN toggled: %d", active);
        app->getFsm().setFanOverride(active);
    });

    app->getInputs().setOnLampSwitchChanged([app](bool active) {
        ESP_LOGI(TAG, "Hardware SW_LAMP toggled: %d", active);
        app->getFsm().setLampOverride(active);
    });

    while (true) {
        // Poll and debounce physical GPIOs
        app->getInputs().update();

        uint64_t nowUs = esp_timer_get_time();

        // ── 1. START BUTTON LOGIC ────────────────────────────────────────────
        bool startHeld = app->getInputs().isStartHeld();
        if (startHeld && !lastStartHeld) {
            // Button just pressed down
            startPressBeginUs = nowUs;
        } else if (!startHeld && lastStartHeld) {
            // Button released -> evaluate press duration
            uint64_t durationMs = (nowUs - startPressBeginUs) / 1000ULL;
            if (durationMs >= 2000) {
                // Long Press (>2s): Start Preheat
                ESP_LOGI(TAG, "START Long Press (%llu ms) -> queue PREHEAT", durationMs);
                std::string profName = app->getContext().getSnapshot().activeProfileFile;
                if (profName.empty()) {
                    ESP_LOGW(TAG, "Hardware Button: START pressed but no profile is selected – aborted.");
                } else {
                    auto* prof = new config::ReflowProfile();
                    if (app->getStorage().loadProfile(profName, *prof)) {
                        FsmCommand cmd{};
                        cmd.type = FsmCommandType::PREHEAT;
                        cmd.profile = prof;
                        if (!fsmCmdPost(cmd)) {
                            delete prof;
                            ESP_LOGE(TAG, "Hardware Button: PREHEAT queue full – profile freed");
                        }
                    } else {
                        delete prof;
                        ESP_LOGW(TAG, "Hardware Button: Active profile '%s' not found in storage", profName.c_str());
                    }
                }
            } else if (durationMs >= 50) {
                // Short Press (<2s): Start Reflow (after Preheat Done)
                ESP_LOGI(TAG, "START Short Press (%llu ms) -> queue REFLOW", durationMs);
                FsmCommand cmd{};
                cmd.type = FsmCommandType::REFLOW;
                fsmCmdPost(cmd);
            }
        }
        lastStartHeld = startHeld;

        // ── 2. STOP BUTTON LOGIC ─────────────────────────────────────────────
        bool stopHeld = app->getInputs().isStopHeld();
        if (stopHeld && !lastStopHeld) {
            stopPressBeginUs = nowUs;
        } else if (!stopHeld && lastStopHeld) {
            uint64_t durationMs = (nowUs - stopPressBeginUs) / 1000ULL;
            auto state = app->getContext().getSnapshot().state;

            if (durationMs >= 2000) {
                // Long Press (>2s): Graceful Stop (transitions to Cooling)
                ESP_LOGI(TAG, "STOP Long Press (%llu ms) -> queue STOP", durationMs);
                FsmCommand cmd{};
                cmd.type = FsmCommandType::STOP;
                fsmCmdPost(cmd);
            } else if (durationMs >= 50) {
                // Short Press (<2s):
                if (state == fsm::ReflowState::FAULT) {
                    ESP_LOGI(TAG, "STOP Short Press in FAULT -> queue RESET_FAULT");
                    FsmCommand cmd{};
                    cmd.type = FsmCommandType::RESET_FAULT;
                    fsmCmdPost(cmd);
                    app->getSafety().reset();
                } else if (state == fsm::ReflowState::PREHEAT ||
                           state == fsm::ReflowState::SOAK ||
                           state == fsm::ReflowState::REFLOW) {
                    ESP_LOGI(TAG, "STOP Short Press during process -> queue SKIP_STEP");
                    FsmCommand cmd{};
                    cmd.type = FsmCommandType::SKIP_STEP;
                    fsmCmdPost(cmd);
                }
            }
        }
        lastStopHeld = stopHeld;

        // Wait until next 20ms cycle
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

} // namespace app
