/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file test_runner.cpp
 * @brief Implementation of Unified Firmware Test Runner.
 *
 * Sequentially executes all component unit tests and prints consolidated reports.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "test_runner.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"

namespace {
constexpr const char* TAG = "TestRunner";
} // namespace

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

// ============================================================================
// High-Visibility Unity Test Failure Interceptor (Wrap Linker Hook)
// ============================================================================

extern "C" {
void __real_UnityDefaultTestRun(UnityTestFunction Func, const char* FuncName, int FuncLineNum);

void __wrap_UnityDefaultTestRun(UnityTestFunction Func, const char* FuncName, const int FuncLineNum) {
    const unsigned int failBefore = Unity.TestFailures;
    __real_UnityDefaultTestRun(Func, FuncName, FuncLineNum);
    // cppcheck-suppress knownConditionTrueFalse
    if (Unity.TestFailures > failBefore) {
        printf("\033[1;35m>>> [UNIT TEST FAILED] %s <<<\033[0m\n", FuncName);
    }
}
}

namespace app {

// ============================================================================
// Master Boot-Time Unity Test Runner
// ============================================================================

bool runAllUnitTests() {
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "      STARTING COMPLETE MODULE UNIT TESTS                   ");
    ESP_LOGI(TAG, "============================================================");

    UNITY_BEGIN();

    run_config_tests();
    vTaskDelay(pdMS_TO_TICKS(1));
    run_simulation_tests();
    vTaskDelay(pdMS_TO_TICKS(1));
    run_output_tests();
    vTaskDelay(pdMS_TO_TICKS(1));
    run_pid_tests();
    vTaskDelay(pdMS_TO_TICKS(1));
    run_autotuner_tests();
    vTaskDelay(pdMS_TO_TICKS(1));
    run_safety_tests();
    vTaskDelay(pdMS_TO_TICKS(1));
    run_sensor_tests();
    vTaskDelay(pdMS_TO_TICKS(1));
    run_input_tests();
    vTaskDelay(pdMS_TO_TICKS(1));
    run_storage_tests();
    vTaskDelay(pdMS_TO_TICKS(1));
    run_fsm_tests();
    vTaskDelay(pdMS_TO_TICKS(1));
    run_web_tests();
    vTaskDelay(pdMS_TO_TICKS(1));
    run_main_integration_tests();
    vTaskDelay(pdMS_TO_TICKS(1));

    int failures = UNITY_END();

    if (failures > 0) {
        printf("\n\033[1;35m############################################################\033[0m\n");
        printf("\033[1;35m###  BOOT TEST FAILURE: %2d UNIT TEST(S) FAILED!          ###\033[0m\n", failures);
        printf("\033[1;35m###  PLEASE CHECK THE ':FAIL' OUTPUT ABOVE FOR DETAILS!  ###\033[0m\n");
        printf("\033[1;35m############################################################\033[0m\n\n");
        ESP_LOGE(TAG, "      UNIT TESTS FINISHED: %d FAILED!                       ", failures);
    } else {
        ESP_LOGI(TAG, "============================================================");
        ESP_LOGI(TAG, "      ALL UNIT TESTS FINISHED — ALL PASSED! (Failures: 0)   ");
        ESP_LOGI(TAG, "============================================================");
    }

    return (failures == 0);
}

} // namespace app
