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
 * @file test_autotuner.cpp
 * @brief Unit tests for pid::PidAutotuner (sTune wrapper).
 *
 * Tests cover:
 *  1. Initial state: not running, not finished, progress = 0
 *  2. begin() sets correct channel and target temperature
 *  3. step() returns true while running and outputs power in valid range
 *  4. abort() immediately stops the tuner and resets output to 0
 *  5. Emergency stop: step() halts when temperature exceeds eStop threshold
 *  6. applyResults() sets gains on PIDController only when finished
 *  7. Bottom channel begin() sets isTop = false correctly
 *  8. Progress is in valid range after step
 *  9. getResults() returns zeros before completion
 * 10. eStop is clamped to config::Limits::MAX_TEMPERATURE for high target temps
 * 11. Channel and target bounds switching
 * 12. Repeated begin() correctly re-initializes tuner
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "unity.h"
#include "pid/pid_autotuner.hpp"
#include "pid/pid_controller.hpp"
#include "config/machine_config.hpp"

// 1. Initial State Test
static void test_autotuner_initial_state()
{
    pid::PidAutotuner tuner;
    TEST_ASSERT_FALSE(tuner.isRunning());
    TEST_ASSERT_FALSE(tuner.isFinished());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, tuner.getProgressPercent());
}

// 2. Begin Configures Channel and Target Test
static void test_autotuner_begin_top()
{
    pid::PidAutotuner tuner;
    tuner.begin(true, 120.0f, 25.0f);
    TEST_ASSERT_TRUE(tuner.isTop());
    TEST_ASSERT_TRUE(tuner.isRunning());
    TEST_ASSERT_FALSE(tuner.isFinished());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, tuner.getTargetTemp());
}

static void test_autotuner_begin_bottom()
{
    pid::PidAutotuner tuner;
    tuner.begin(false, 120.0f, 30.0f);
    TEST_ASSERT_FALSE(tuner.isTop());
    TEST_ASSERT_TRUE(tuner.isRunning());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, tuner.getTargetTemp());
}

// 3. Step Output Clamping and Running State Test
static void test_autotuner_step_outputs_valid_range()
{
    pid::PidAutotuner tuner;
    tuner.begin(true, 150.0f, 25.0f);

    // Feed temperature well below eStop (150 + 40 = 190°C)
    float outPower = -1.0f;
    bool running = tuner.step(30.0f, outPower);

    TEST_ASSERT_TRUE(running);
    TEST_ASSERT_TRUE(outPower >= 0.0f);
    TEST_ASSERT_TRUE(outPower <= 100.0f);
}

static void test_autotuner_step_not_running_returns_false()
{
    pid::PidAutotuner tuner;
    // step() before begin() must return false and set output = 0
    float outPower = 99.0f;
    bool running = tuner.step(25.0f, outPower);
    TEST_ASSERT_FALSE(running);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, outPower);
}

// 4. Abort Stops Tuner Test
static void test_autotuner_abort_stops_running()
{
    pid::PidAutotuner tuner;
    tuner.begin(true, 150.0f, 25.0f);
    TEST_ASSERT_TRUE(tuner.isRunning());

    tuner.abort();
    TEST_ASSERT_FALSE(tuner.isRunning());
    TEST_ASSERT_FALSE(tuner.isFinished()); // Aborted != finished

    // step() after abort must return false and output 0
    float outPower = 50.0f;
    bool running = tuner.step(30.0f, outPower);
    TEST_ASSERT_FALSE(running);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, outPower);
}

// 5. Emergency Stop Threshold Test
static void test_autotuner_estop_triggers_above_threshold()
{
    pid::PidAutotuner tuner;
    // Target 150°C -> eStop at 190°C
    tuner.begin(true, 150.0f, 25.0f);

    float outPower = 99.0f;
    const float eStopTemp = 150.0f + 40.0f + 1.0f; // 191°C – just above limit

    bool running = tuner.step(eStopTemp, outPower);

    TEST_ASSERT_FALSE(running);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, outPower);
}

// 6. Apply Results Guard Test
static void test_autotuner_apply_results_only_when_finished()
{
    pid::PidAutotuner tuner;
    pid::PIDController pid(1.0f, 0.1f, 0.01f);
    pid.begin();

    // Not finished: applyResults must be a safe no-op
    tuner.applyResults(pid); // Must not crash or change gains
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, pid.getKp());
}

// 7. Progress Range Test
static void test_autotuner_progress_in_valid_range()
{
    pid::PidAutotuner tuner;
    tuner.begin(true, 150.0f, 25.0f);

    float outPower = 0.0f;
    tuner.step(30.0f, outPower);

    float progress = tuner.getProgressPercent();
    TEST_ASSERT_TRUE(progress >= 0.0f);
    TEST_ASSERT_TRUE(progress <= 100.0f);
}

// 8. Results Zero Before Completion Test
static void test_autotuner_results_zero_before_finish()
{
    pid::PidAutotuner tuner;
    tuner.begin(true, 150.0f, 25.0f);

    float kp = 99.0f, ki = 99.0f, kd = 99.0f;
    tuner.getResults(kp, ki, kd);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, kp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ki);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, kd);
}

// 9. EStop Clamped at Max Limit Test
static void test_autotuner_estop_clamped_at_max()
{
    pid::PidAutotuner tuner;
    // Target MAX_TEMPERATURE (300°C) -> 300 + 40 = 340°C, must be clamped to MAX_TEMPERATURE (300°C)
    tuner.begin(true, config::Limits::MAX_TEMPERATURE, 25.0f);
    TEST_ASSERT_TRUE(tuner.isRunning());

    // Feed MAX_TEMPERATURE + 1.0f – above clamped eStop
    float outPower = 99.0f;
    bool running = tuner.step(config::Limits::MAX_TEMPERATURE + 1.0f, outPower);
    TEST_ASSERT_FALSE(running);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, outPower);
}

// 10. Channel and Target Bounds Test
static void test_autotuner_channel_and_target_bounds()
{
    pid::PidAutotuner tuner;
    tuner.begin(true, 180.0f, 25.0f);
    TEST_ASSERT_TRUE(tuner.isTop());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, tuner.getTargetTemp());

    tuner.abort();

    tuner.begin(false, 200.0f, 25.0f);
    TEST_ASSERT_FALSE(tuner.isTop());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, tuner.getTargetTemp());
}

// 11. Re-initialization Reset Test
static void test_autotuner_reinitialization()
{
    pid::PidAutotuner tuner;
    tuner.begin(true, 150.0f, 25.0f);
    TEST_ASSERT_TRUE(tuner.isRunning());

    float outPower = 0.0f;
    tuner.step(30.0f, outPower);

    // Call begin() again while running to reinitialize
    tuner.begin(false, 180.0f, 40.0f);
    TEST_ASSERT_TRUE(tuner.isRunning());
    TEST_ASSERT_FALSE(tuner.isFinished());
    TEST_ASSERT_FALSE(tuner.isTop());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, tuner.getTargetTemp());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, tuner.getProgressPercent());
}

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_autotuner_tests()
{
    RUN_TEST(test_autotuner_initial_state);
    RUN_TEST(test_autotuner_begin_top);
    RUN_TEST(test_autotuner_begin_bottom);
    RUN_TEST(test_autotuner_step_outputs_valid_range);
    RUN_TEST(test_autotuner_step_not_running_returns_false);
    RUN_TEST(test_autotuner_abort_stops_running);
    RUN_TEST(test_autotuner_estop_triggers_above_threshold);
    RUN_TEST(test_autotuner_apply_results_only_when_finished);
    RUN_TEST(test_autotuner_progress_in_valid_range);
    RUN_TEST(test_autotuner_results_zero_before_finish);
    RUN_TEST(test_autotuner_estop_clamped_at_max);
    RUN_TEST(test_autotuner_channel_and_target_bounds);
    RUN_TEST(test_autotuner_reinitialization);
}
