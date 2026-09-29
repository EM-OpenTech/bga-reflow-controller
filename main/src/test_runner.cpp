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
    ESP_LOGI(TAG, "      STARTING COMPLETE MODULE UNIT TESTS                   ");
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
