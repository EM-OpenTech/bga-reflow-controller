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

// 1. Proportional Action Test
static void test_pid_proportional()
{
    // Real-world ceramic heater gains: Kp=2.5, Ki=0.5, Kd=1.0 (pOnMeas, Control::timer)
    pid::PIDController pid(2.5f, 0.5f, 1.0f);
    pid.begin();
    pid.setAutomatic(true);
    pid.setOutputLimits(0.0f, 100.0f);
    pid.setSetpoint(150.0f);
    pid.setInput(100.0f); // Positive error (+50 C)

    // Simulate 5 ticks of the 200ms FreeRTOS control task
    for (int i = 0; i < 5; ++i) {
        pid.compute();
    }
    float out = pid.getOutput();
    TEST_ASSERT_TRUE(out > 0.0f);
    TEST_ASSERT_TRUE(out <= 100.0f);
}

// 2. Clamping Limits Test
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

// 3. Manual Mode Override Test
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

// 4. Tuning Gains Update Test
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

// 5. Bumpless Transfer Test
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

// 6. Controller Reset Test
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

// 7. Diagnostic Terms Test
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
    TEST_ASSERT_EQUAL_UINT32(200, pid.getSampleTimeMs());
}

// 8. Anti-Windup Clamping (iAwClamp) Deep Test
static void test_pid_anti_windup_clamping()
{
    pid::PIDController pid(3.0f, 0.5f, 1.0f);
    pid.begin();
    pid.setAutomatic(true);
    pid.setOutputLimits(0.0f, 100.0f);
    pid.setSetpoint(220.0f);
    pid.setInput(30.0f); // Massive +190°C error

    // Run 100 ticks (10 simulated seconds of continuous saturation)
    for (int tick = 0; tick < 100; ++tick) {
        pid.compute();
    }

    // Output must clamp strictly at 100.0%
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, pid.getOutput());

    // Internal output sum must not blow up beyond limits (anti-windup active)
    TEST_ASSERT_TRUE(pid.getOutputSum() <= 100.01f);

    // Immediate recovery when process temperature overshoots setpoint
    pid.setInput(230.0f); // Process crossed setpoint (+10°C overshoot)
    pid.compute();

    // Controller must immediately reduce output below 100% without windup delay
    TEST_ASSERT_TRUE(pid.getOutput() < 100.0f);
}

// 9. Derivative-on-Measurement (dOnMeas) Setpoint Step Kick Protection
static void test_pid_derivative_kick_protection()
{
    pid::PIDController pid(2.5f, 0.2f, 5.0f); // High Kd to detect derivative spikes
    pid.begin();
    pid.setAutomatic(true);
    pid.setOutputLimits(0.0f, 100.0f);
    pid.setSetpoint(100.0f);
    pid.setInput(100.0f);
    pid.compute();

    // Sudden massive step change in setpoint: 100°C -> 250°C (+150°C step)
    // Process input remains steady at 100°C
    pid.setSetpoint(250.0f);
    pid.setInput(100.0f);
    pid.compute();

    // Since derivative is calculated on measurement (dOnMeas), the D-term must remain 0
    // (no derivative spike kick from setpoint discontinuity)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, pid.getDterm());
}

// 10. Gain Scheduling Dynamic Update Test Under Active Load
static void test_pid_gain_scheduling_smoothness()
{
    pid::PIDController pid(1.5f, 0.02f, 0.5f);
    pid.begin();
    pid.setAutomatic(true);
    pid.setOutputLimits(0.0f, 100.0f);
    pid.setSetpoint(160.0f);
    pid.setInput(140.0f);

    for (int i = 0; i < 20; ++i) {
        pid.compute();
    }
    float outBefore = pid.getOutput();
    TEST_ASSERT_TRUE(outBefore > 0.0f && outBefore < 100.0f);

    // Switch to Aggressive Reflow Tuning (Kp=4.0, Ki=0.1, Kd=2.0)
    pid.setTunings(4.0f, 0.1f, 2.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, pid.getKp());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, pid.getKi());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, pid.getKd());

    pid.compute();
    float outAfter = pid.getOutput();

    // Must be a valid, finite number within bounds without NaN or discontinuity
    TEST_ASSERT_FALSE(std::isnan(outAfter));
    TEST_ASSERT_TRUE(outAfter >= 0.0f && outAfter <= 100.0f);
}

// 11. Custom Constrained Output Limits Test
static void test_pid_custom_output_limits()
{
    pid::PIDController pid(5.0f, 1.0f, 1.0f);
    pid.begin();
    pid.setAutomatic(true);
    pid.setOutputLimits(15.0f, 75.0f); // Non-standard clamp window

    // Massive positive error
    pid.setSetpoint(300.0f);
    pid.setInput(20.0f);
    for (int i = 0; i < 15; ++i) {
        pid.compute();
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f, pid.getOutput());

    // Massive negative error
    pid.setSetpoint(20.0f);
    pid.setInput(300.0f);
    for (int i = 0; i < 15; ++i) {
        pid.compute();
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, pid.getOutput());
}

// 12. Closed-Loop Thermal Inertia Convergence Simulation
static void test_pid_thermal_inertia_simulation()
{
    pid::PIDController pid(3.0f, 0.35f, 1.2f);
    pid.begin();
    pid.setAutomatic(true);
    pid.setOutputLimits(0.0f, 100.0f);

    const float targetTemp = 150.0f;
    pid.setSetpoint(targetTemp);

    float simTemp = 25.0f; // Ambient start temperature (25°C)

    // Simulate 350 ticks (70.0 seconds of closed-loop regulation at 5 Hz / 200 ms)
    for (int tick = 0; tick < 350; ++tick) {
        pid.setInput(simTemp);
        pid.compute();
        float power = pid.getOutput();

        // 1st order thermal lag model with dt = 0.2s: heating gain + ambient cooling loss
        float dTemp = (power * 0.20f) - (simTemp - 25.0f) * 0.02f;
        simTemp += dTemp;
    }

    // Verify convergence: Temperature must approach target within ±5°C after settling
    TEST_ASSERT_FLOAT_WITHIN(5.0f, targetTemp, simTemp);
    TEST_ASSERT_TRUE(pid.getOutput() > 0.0f && pid.getOutput() < 100.0f);
}

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_pid_tests()
{
    RUN_TEST(test_pid_proportional);
    RUN_TEST(test_pid_clamping);
    RUN_TEST(test_pid_manual);
    RUN_TEST(test_pid_set_tunings);
    RUN_TEST(test_pid_bumpless_transfer);
    RUN_TEST(test_pid_reset);
    RUN_TEST(test_pid_diagnostics);
    RUN_TEST(test_pid_anti_windup_clamping);
    RUN_TEST(test_pid_derivative_kick_protection);
    RUN_TEST(test_pid_gain_scheduling_smoothness);
    RUN_TEST(test_pid_custom_output_limits);
    RUN_TEST(test_pid_thermal_inertia_simulation);
}
