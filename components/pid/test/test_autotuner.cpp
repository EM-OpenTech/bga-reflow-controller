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

#include "config/machine_config.hpp"
#include "pid/pid_autotuner.hpp"
#include "pid/pid_controller.hpp"
#include "sTan.hpp"
#include "sTune.hpp"
#include "unity.h"
#include <limits>

// 1. Initial State Test
static void test_autotuner_initial_state() {
    pid::PidAutotuner tuner;
    TEST_ASSERT_FALSE(tuner.isRunning());
    TEST_ASSERT_FALSE(tuner.isFinished());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, tuner.getProgressPercent());
}

// 2. Begin Configures Channel and Target Test
static void test_autotuner_begin_top() {
    pid::PidAutotuner tuner;
    tuner.begin(true, 120.0f, 25.0f);
    TEST_ASSERT_TRUE(tuner.isTop());
    TEST_ASSERT_TRUE(tuner.isRunning());
    TEST_ASSERT_FALSE(tuner.isFinished());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, tuner.getTargetTemp());
}

static void test_autotuner_begin_bottom() {
    pid::PidAutotuner tuner;
    tuner.begin(false, 120.0f, 30.0f);
    TEST_ASSERT_FALSE(tuner.isTop());
    TEST_ASSERT_TRUE(tuner.isRunning());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, tuner.getTargetTemp());
}

// 3. Step Output Clamping and Running State Test
static void test_autotuner_step_outputs_valid_range() {
    pid::PidAutotuner tuner;
    tuner.begin(true, 150.0f, 25.0f);

    // Feed temperature well below eStop (150 + 40 = 190°C)
    float outPower = -1.0f;
    bool running = tuner.step(30.0f, outPower);

    TEST_ASSERT_TRUE(running);
    TEST_ASSERT_TRUE(outPower >= 0.0f);
    TEST_ASSERT_TRUE(outPower <= 100.0f);
}

static void test_autotuner_step_not_running_returns_false() {
    pid::PidAutotuner tuner;
    // step() before begin() must return false and set output = 0
    float outPower = 99.0f;
    bool running = tuner.step(25.0f, outPower);
    TEST_ASSERT_FALSE(running);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, outPower);
}

// 4. Abort Stops Tuner Test
static void test_autotuner_abort_stops_running() {
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
static void test_autotuner_estop_triggers_above_threshold() {
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
static void test_autotuner_apply_results_only_when_finished() {
    pid::PidAutotuner tuner;
    pid::PIDController pid(1.0f, 0.1f, 0.01f);
    pid.begin();

    // Not finished: applyResults must be a safe no-op
    tuner.applyResults(pid); // Must not crash or change gains
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, pid.getKp());
}

// 7. Progress Range Test
static void test_autotuner_progress_in_valid_range() {
    pid::PidAutotuner tuner;
    tuner.begin(true, 150.0f, 25.0f);

    float outPower = 0.0f;
    tuner.step(30.0f, outPower);

    float progress = tuner.getProgressPercent();
    TEST_ASSERT_TRUE(progress >= 0.0f);
    TEST_ASSERT_TRUE(progress <= 100.0f);
}

// 8. Results Zero Before Completion Test
static void test_autotuner_results_zero_before_finish() {
    pid::PidAutotuner tuner;
    tuner.begin(true, 150.0f, 25.0f);

    float kp = 99.0f;
    float ki = 99.0f;
    float kd = 99.0f;
    tuner.getResults(kp, ki, kd);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, kp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ki);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, kd);
}

// 9. EStop Clamped at Max Limit Test
static void test_autotuner_estop_clamped_at_max() {
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
static void test_autotuner_channel_and_target_bounds() {
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
static void test_autotuner_reinitialization() {
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

// 12. NaN Input Safety Test
static void test_autotuner_nan_safety() {
    pid::PidAutotuner tuner;
    tuner.begin(true, 150.0f, 25.0f);
    TEST_ASSERT_TRUE(tuner.isRunning());

    float outPower = 50.0f;
    float qnan = std::numeric_limits<float>::quiet_NaN();

    // Passing NaN as currentTemp must trigger immediate abort and 0 power
    bool cont = tuner.step(qnan, outPower);
    TEST_ASSERT_FALSE(cont);
    TEST_ASSERT_FALSE(tuner.isRunning());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, outPower);
}

// 15. Infinity Input Safety Test
static void test_autotuner_inf_safety() {
    pid::PidAutotuner tuner;
    tuner.begin(true, 150.0f, 25.0f);
    TEST_ASSERT_TRUE(tuner.isRunning());

    float outPower = 50.0f;
    float posInf = std::numeric_limits<float>::infinity();

    // Passing Infinity as currentTemp must trigger immediate emergency stop
    bool cont = tuner.step(posInf, outPower);
    TEST_ASSERT_FALSE(cont);
    TEST_ASSERT_FALSE(tuner.isRunning());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, outPower);
}

// 16. NaN / Inf Begin Parameters Rejection Test
static void test_autotuner_nan_begin_rejected() {
    pid::PidAutotuner tuner;
    float qnan = std::numeric_limits<float>::quiet_NaN();
    float posInf = std::numeric_limits<float>::infinity();

    // Passing NaN as target temperature refuses to start
    tuner.begin(true, qnan, 25.0f);
    TEST_ASSERT_FALSE(tuner.isRunning());

    // Passing Inf as start temperature refuses to start
    tuner.begin(true, 150.0f, posInf);
    TEST_ASSERT_FALSE(tuner.isRunning());
}

// 17. Tuning Method Selection & Dynamic Runtime Switching
static void test_autotuner_method_selection() {
    pid::PidAutotuner tuner;
    TEST_ASSERT_EQUAL_UINT8(sTune::Mixed_PID, tuner.getTuningMethod());

    tuner.begin(true, 150.0f, 25.0f, sTune::CohenCoon_PID);
    TEST_ASSERT_EQUAL_UINT8(sTune::CohenCoon_PID, tuner.getTuningMethod());

    tuner.begin(false, 140.0f, 25.0f, sTune::DampedOsc_PID);
    TEST_ASSERT_EQUAL_UINT8(sTune::DampedOsc_PID, tuner.getTuningMethod());

    tuner.begin(true, 160.0f, 25.0f, sTune::ZN_PID);
    TEST_ASSERT_EQUAL_UINT8(sTune::ZN_PID, tuner.getTuningMethod());

    tuner.begin(false, 130.0f, 25.0f, sTune::NoOvershoot_PID);
    TEST_ASSERT_EQUAL_UINT8(sTune::NoOvershoot_PID, tuner.getTuningMethod());

    tuner.begin(true, 150.0f, 25.0f); // Default parameter
    TEST_ASSERT_EQUAL_UINT8(sTune::Mixed_PID, tuner.getTuningMethod());

    TEST_ASSERT_EQUAL_STRING("Mixed_PID", pid::getTuningMethodName(sTune::Mixed_PID));
    TEST_ASSERT_EQUAL_STRING("CohenCoon_PID", pid::getTuningMethodName(sTune::CohenCoon_PID));
    TEST_ASSERT_EQUAL_STRING("DampedOsc_PID", pid::getTuningMethodName(sTune::DampedOsc_PID));
    TEST_ASSERT_EQUAL_STRING("NoOvershoot_PID", pid::getTuningMethodName(sTune::NoOvershoot_PID));
    TEST_ASSERT_EQUAL_STRING("ZN_PID", pid::getTuningMethodName(sTune::ZN_PID));
    TEST_ASSERT_EQUAL_STRING("Unknown", pid::getTuningMethodName(99));
}

// 17. sTan Sliding Tangent Circular Buffer Math & Bounds Test
static void test_stune_stan_circular_buffer() {
    sTan tanBuf(5);
    TEST_ASSERT_EQUAL_UINT16(5, tanBuf.length());

    // Initialize with 20.0°C
    tanBuf.init(20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, tanBuf.startVal());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, tanBuf.slope(20.0f));

    // Push progressive samples: 21, 22, 23, 24, 25
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.2f, tanBuf.avgVal(21.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.6f, tanBuf.avgVal(22.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.2f, tanBuf.avgVal(23.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, tanBuf.avgVal(24.0f));
    float avg = tanBuf.avgVal(25.0f);

    // Sum should be 21+22+23+24+25 = 115, avg = 23.0
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.0f, avg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.0f, tanBuf.startVal());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, tanBuf.slope(25.0f));

    // NaN / Inf protection: pushing NaN or Inf should not corrupt buffer
    float qnan = std::numeric_limits<float>::quiet_NaN();
    avg = tanBuf.avgVal(qnan);
    TEST_ASSERT_FALSE(std::isnan(avg));
    TEST_ASSERT_FALSE(std::isinf(avg));

    // Empty buffer guards
    sTan emptyBuf;
    TEST_ASSERT_EQUAL_UINT16(0, emptyBuf.length());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, emptyBuf.avgVal(15.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, emptyBuf.startVal());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, emptyBuf.slope(15.0f));
}

// 18. sTune Core Lifecycle and Configuration Test
static void test_stune_lifecycle_and_config() {
    float inVal = 25.0f;
    float outVal = 0.0f;
    sTune tuner(&inVal, &outVal, sTune::CohenCoon_PID, sTune::directIP, sTune::printOFF);

    TEST_ASSERT_EQUAL_UINT8(sTune::directIP, tuner.GetControllerAction());
    TEST_ASSERT_EQUAL_UINT8(sTune::printOFF, tuner.GetSerialMode());
    TEST_ASSERT_EQUAL_UINT8(sTune::CohenCoon_PID, tuner.GetTuningMethod());

    tuner.Configure(200.0f, 100.0f, 0.0f, 80.0f, 60, 5, 50);
    TEST_ASSERT_EQUAL_UINT16(50, tuner.GetTotalSamples());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, tuner.GetProgress());

    tuner.SetControllerAction(sTune::direct5T);
    TEST_ASSERT_EQUAL_UINT8(sTune::direct5T, tuner.GetControllerAction());

    tuner.SetSerialMode(sTune::printSUMMARY);
    TEST_ASSERT_EQUAL_UINT8(sTune::printSUMMARY, tuner.GetSerialMode());

    tuner.SetTuningMethod(sTune::NoOvershoot_PID);
    TEST_ASSERT_EQUAL_UINT8(sTune::NoOvershoot_PID, tuner.GetTuningMethod());

    tuner.SetEmergencyStop(220.0f);
    tuner.Reset();
    TEST_ASSERT_EQUAL_UINT16(0, tuner.GetSampleCount());
}

// 19. sTune NaN and Inf Rejection Test
static void test_stune_nan_inf_input_handling() {
    float inVal = std::numeric_limits<float>::quiet_NaN();
    float outVal = 0.0f;
    sTune tuner(&inVal, &outVal, sTune::ZN_PID, sTune::directIP, sTune::printOFF);
    tuner.Configure(200.0f, 100.0f, 0.0f, 80.0f, 60, 5, 50);

    // Run() with NaN input must return current status without crash or NaN propagation
    uint8_t status = tuner.Run();
    TEST_ASSERT_EQUAL_UINT8(sTune::test, status);

    // Run() with Inf input
    inVal = std::numeric_limits<float>::infinity();
    status = tuner.Run();
    TEST_ASSERT_EQUAL_UINT8(sTune::test, status);

    // Run() with null pointers returns timerPid
    sTune nullTuner(nullptr, nullptr, sTune::ZN_PID, sTune::directIP, sTune::printOFF);
    TEST_ASSERT_EQUAL_UINT8(sTune::timerPid, nullTuner.Run());
}

// 20. sTune All Tuning Methods Mathematical Bounds Test
static void test_stune_tuning_methods_gains() {
    float inVal = 25.0f;
    float outVal = 0.0f;
    sTune tuner(&inVal, &outVal, sTune::ZN_PID, sTune::directIP, sTune::printOFF);
    tuner.Configure(200.0f, 100.0f, 0.0f, 80.0f, 60, 5, 50);

    const sTune::TuningMethod methods[] = {
        sTune::ZN_PID, sTune::DampedOsc_PID, sTune::NoOvershoot_PID, sTune::CohenCoon_PID, sTune::Mixed_PID,
        sTune::ZN_PI,  sTune::DampedOsc_PI,  sTune::NoOvershoot_PI,  sTune::CohenCoon_PI,  sTune::Mixed_PI};

    for (auto method : methods) {
        tuner.SetTuningMethod(method);
        float kp = tuner.GetKp();
        float ki = tuner.GetKi();
        float kd = tuner.GetKd();
        float ti = tuner.GetTi();
        float td = tuner.GetTd();

        // Gains must be strictly finite (not NaN, not Inf) and non-negative
        TEST_ASSERT_FALSE(std::isnan(kp));
        TEST_ASSERT_FALSE(std::isinf(kp));
        TEST_ASSERT_TRUE(kp >= 0.0f);

        TEST_ASSERT_FALSE(std::isnan(ki));
        TEST_ASSERT_FALSE(std::isinf(ki));
        TEST_ASSERT_TRUE(ki >= 0.0f);

        TEST_ASSERT_FALSE(std::isnan(kd));
        TEST_ASSERT_FALSE(std::isinf(kd));
        TEST_ASSERT_TRUE(kd >= 0.0f);

        TEST_ASSERT_FALSE(std::isnan(ti));
        TEST_ASSERT_FALSE(std::isinf(ti));
        TEST_ASSERT_TRUE(ti >= 0.0f);

        TEST_ASSERT_FALSE(std::isnan(td));
        TEST_ASSERT_FALSE(std::isinf(td));
        TEST_ASSERT_TRUE(td >= 0.0f);
    }
}

// 21. sTune Gain Extraction Output Array Test
static void test_stune_get_auto_tunings() {
    float inVal = 25.0f;
    float outVal = 0.0f;
    sTune tuner(&inVal, &outVal, sTune::ZN_PID, sTune::directIP, sTune::printOFF);

    float kp = -1.0f;
    float ki = -1.0f;
    float kd = -1.0f;
    tuner.GetAutoTunings(&kp, &ki, &kd);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, kp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ki);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, kd);

    // Nullptr guard: must not crash
    tuner.GetAutoTunings(nullptr, nullptr, nullptr);
}

// 22. sTune Cohen-Coon and Parallel Gain Conversion Test
static void test_stune_cohen_coon_and_parallel_conversion() {
    float inVal = 25.0f;
    float outVal = 0.0f;
    sTune tuner(&inVal, &outVal, sTune::CohenCoon_PID, sTune::directIP, sTune::printOFF);

    // Simulated FOPDT parameters: Ku = 2.0 °C/%, Tu = 50.0s, td = 5.0s -> R = 0.10
    tuner.setIdentifiedParametersForTest(2.0f, 50.0f, 5.0f, 0.10f);

    // Cohen-Coon:
    // Kp = (1 / (Ku * R)) * (1.333 + R / 4) = (1 / 0.20) * (1.333 + 0.025) = 5.0 * 1.358 = 6.79
    // Ti = td * (30 + 3*R) / (9 + 20*R) = 5 * 30.3 / 11 = 13.77s
    // Ki_par = Kp / Ti = 6.79 / 13.77 = 0.493
    // Td = 4 * td / (11 + 2*R) = 20 / 11.2 = 1.786s
    // Kd_par = Kp * Td = 6.79 * 1.786 = 12.13
    float ccKp = tuner.GetKp();
    float ccKi = tuner.GetKi();
    float ccKd = tuner.GetKd();
    float ccTi = tuner.GetTi();
    float ccTd = tuner.GetTd();

    TEST_ASSERT_FLOAT_WITHIN(0.05f, 6.79f, ccKp);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 13.77f, ccTi);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.493f, ccKi);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.786f, ccTd);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 12.13f, ccKd);

    // Ziegler-Nichols BGA (damped):
    // Kp = 0.6 * Tu / (Ku * td) = 0.6 * 50 / 10 = 3.0
    // Ti = 2 * td = 10s -> Ki_par = 3.0 / 10 = 0.30
    // Td = 0.5 * td = 2.5s -> Kd_par = 3.0 * 2.5 = 7.5
    tuner.SetTuningMethod(sTune::ZN_PID);
    float znKp = tuner.GetKp();
    float znKi = tuner.GetKi();
    float znKd = tuner.GetKd();
    float znTi = tuner.GetTi();
    float znTd = tuner.GetTd();

    TEST_ASSERT_FLOAT_WITHIN(0.05f, 3.0f, znKp);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 10.0f, znTi);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.30f, znKi);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 2.5f, znTd);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 7.5f, znKd);
}

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_autotuner_tests() {
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
    RUN_TEST(test_autotuner_nan_safety);
    RUN_TEST(test_autotuner_inf_safety);
    RUN_TEST(test_autotuner_nan_begin_rejected);
    RUN_TEST(test_autotuner_method_selection);
    RUN_TEST(test_stune_stan_circular_buffer);
    RUN_TEST(test_stune_lifecycle_and_config);
    RUN_TEST(test_stune_nan_inf_input_handling);
    RUN_TEST(test_stune_tuning_methods_gains);
    RUN_TEST(test_stune_get_auto_tunings);
    RUN_TEST(test_stune_cohen_coon_and_parallel_conversion);
}
