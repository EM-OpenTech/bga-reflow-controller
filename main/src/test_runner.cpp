#include "test_runner.hpp"
#include "unity.h"
#include "esp_log.h"

static const char* TAG = "TestRunner";

// Declarations of module test runners
extern void run_config_tests();
extern void run_simulation_tests();
extern void run_output_tests();
extern void run_pid_tests();
extern void run_autotuner_tests();
extern void run_safety_tests();
extern void run_sensor_tests();
extern void run_input_tests();
extern void run_storage_tests();
extern void run_fsm_tests();
extern void run_web_tests();
extern void run_main_integration_tests();

namespace app {

bool runAllUnitTests()
{
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "      STARTING ESP-IDF v6.0.2 COMPLETE MODULE UNIT TESTS     ");
    ESP_LOGI(TAG, "============================================================");

    UNITY_BEGIN();

    run_config_tests();
    run_simulation_tests();
    run_output_tests();
    run_pid_tests();
    run_autotuner_tests();
    run_safety_tests();
    run_sensor_tests();
    run_input_tests();
    run_storage_tests();
    run_fsm_tests();
    run_web_tests();
    run_main_integration_tests();

    int failures = UNITY_END();

    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "      ALL UNIT TESTS FINISHED (Failures: %d)                ", failures);
    ESP_LOGI(TAG, "============================================================");

    return (failures == 0);
}

} // namespace app
