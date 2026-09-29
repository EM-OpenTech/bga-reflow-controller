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
 * @file test_pid.cpp
 * @brief Unit tests for PIDController component.
 *
 * Tests automatic regulation, output clamping (0-100%), manual mode overrides,
 * gain scheduling updates, bumpless transfer, reset behavior, and diagnostic terms.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "unity.h"
#include "pid/pid_controller.hpp"
#include <cmath>

static void test_pid_proportional()
{
    // Real-world ceramic heater gains: Kp=2.5, Ki=0.5, Kd=1.0 (pOnMeas, Control::timer)
    pid::PIDController pid(2.5f, 0.5f, 1.0f);
    pid.begin();
    pid.setAutomatic(true);
    pid.setOutputLimits(0.0f, 100.0f);
    pid.setSetpoint(150.0f);
    pid.setInput(100.0f); // Positive error (+50 C)

    // Simulate 5 ticks of the 100ms FreeRTOS control task
    for (int i = 0; i < 5; ++i) {
        pid.compute();
    }
    float out = pid.getOutput();
    TEST_ASSERT_TRUE(out > 0.0f);
    TEST_ASSERT_TRUE(out <= 100.0f);
}

static void test_pid_clamping()
{
    pid::PIDController pid(5.0f, 1.0f, 1.0f);
    pid.begin();
    pid.setAutomatic(true);
    pid.setOutputLimits(0.0f, 100.0f);

    // Large positive error: Setpoint 300, Input 25
    pid.setSetpoint(300.0f);
    pid.setInput(25.0f);
    for (int i = 0; i < 20; ++i) {
        pid.compute();
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, pid.getOutput());

    // Large negative error: Setpoint 50, Input 200
    pid.setSetpoint(50.0f);
    pid.setInput(200.0f);
    for (int i = 0; i < 20; ++i) {
        pid.compute();
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, pid.getOutput());
}

static void test_pid_manual()
{
    pid::PIDController pid(2.0f, 0.5f, 0.1f);
    pid.begin();
    pid.setAutomatic(false);
    pid.setManualOutput(45.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.5f, pid.getOutput());
    pid.compute();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.5f, pid.getOutput());
}

static void test_pid_set_tunings()
{
    pid::PIDController pid(2.0f, 0.1f, 0.5f);
    pid.begin();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, pid.getKp());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, pid.getKi());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, pid.getKd());

    pid.setTunings(4.5f, 0.02f, 1.8f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.5f, pid.getKp());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.02f, pid.getKi());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.8f, pid.getKd());
}

static void test_pid_bumpless_transfer()
{
    pid::PIDController pid(2.5f, 0.5f, 1.0f);
    pid.begin();
    pid.setAutomatic(false);
    pid.setManualOutput(60.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, pid.getOutput());

    // Switch to automatic mode -> outputSum was synced so no initial step jump
    pid.setAutomatic(true);
    TEST_ASSERT_TRUE(pid.isAutomatic());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 60.0f, pid.getOutputSum());
}

static void test_pid_reset()
{
    pid::PIDController pid(3.0f, 0.5f, 1.0f);
    pid.begin();
    pid.setAutomatic(true);
    pid.setSetpoint(200.0f);
    pid.setInput(50.0f);
    for (int i = 0; i < 10; ++i) {
        pid.compute();
    }
    TEST_ASSERT_TRUE(pid.getOutput() > 0.0f);

    pid.reset();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, pid.getOutput());
}

static void test_pid_diagnostics()
{
    pid::PIDController pid(2.0f, 0.1f, 0.5f);
    pid.begin();
    pid.setAutomatic(true);
    pid.setSetpoint(150.0f);
    pid.setInput(100.0f);
    pid.compute();

    // Verify diagnostic terms return finite values
    float p = pid.getPterm();
    float i = pid.getIterm();
    float d = pid.getDterm();
    float sum = pid.getOutputSum();
    TEST_ASSERT_FALSE(std::isnan(p));
    TEST_ASSERT_FALSE(std::isnan(i));
    TEST_ASSERT_FALSE(std::isnan(d));
    TEST_ASSERT_FALSE(std::isnan(sum));
    TEST_ASSERT_EQUAL_UINT32(100, pid.getSampleTimeMs());
}

void run_pid_tests()
{
    RUN_TEST(test_pid_proportional);
    RUN_TEST(test_pid_clamping);
    RUN_TEST(test_pid_manual);
    RUN_TEST(test_pid_set_tunings);
    RUN_TEST(test_pid_bumpless_transfer);
    RUN_TEST(test_pid_reset);
    RUN_TEST(test_pid_diagnostics);
}
