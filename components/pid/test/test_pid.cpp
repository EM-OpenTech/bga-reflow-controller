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

#include "pid/gain_scheduler.hpp"
#include "pid/pid_controller.hpp"
#include "unity.h"
#include <array>
#include <cmath>
#include <limits>

// 1. Proportional Action Test
static void test_pid_proportional() {
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
static void test_pid_clamping() {
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
static void test_pid_manual() {
    pid::PIDController pid(2.0f, 0.5f, 0.1f);
    pid.begin();
    pid.setAutomatic(false);
    pid.setManualOutput(45.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.5f, pid.getOutput());
    pid.compute();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.5f, pid.getOutput());
}

// 4. Tuning Gains Update Test
static void test_pid_set_tunings() {
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
static void test_pid_bumpless_transfer() {
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
static void test_pid_reset() {
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
static void test_pid_diagnostics() {
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
static void test_pid_anti_windup_clamping() {
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
static void test_pid_derivative_kick_protection() {
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
static void test_pid_gain_scheduling_smoothness() {
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
static void test_pid_custom_output_limits() {
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
static void test_pid_thermal_inertia_simulation() {
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
        float dTemp = (power * 0.20f) - ((simTemp - 25.0f) * 0.02f);
        simTemp += dTemp;
    }

    // Verify convergence: Temperature must approach target within ±5°C after settling
    TEST_ASSERT_FLOAT_WITHIN(5.0f, targetTemp, simTemp);
    TEST_ASSERT_TRUE(pid.getOutput() > 0.0f && pid.getOutput() < 100.0f);
}

// 13. NaN Input & Setpoint Rejection Test
static void test_pid_nan_safety() {
    pid::PIDController pid(2.5f, 0.5f, 1.0f);
    pid.begin();
    pid.setSetpoint(150.0f);
    pid.setInput(100.0f);

    float qnan = std::numeric_limits<float>::quiet_NaN();

    // Passing NaN to setInput must not corrupt internal state
    pid.setInput(qnan);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, pid.getInput());

    // Passing NaN to setSetpoint must not corrupt internal state
    pid.setSetpoint(qnan);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, pid.getSetpoint());

    // Passing NaN to setTunings must not corrupt gains
    pid.setTunings(qnan, 0.5f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, pid.getKp());

    // Passing NaN to setManualOutput must not corrupt output
    pid.setManualOutput(qnan);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, pid.getOutput());
}

// 14. GainScheduler Comprehensive Interpolation and Boundary Tests
static void test_pid_gain_scheduler_interpolation() {
    pid::PidGains fallback{1.0f, 0.05f, 0.2f};

    // 1. Null / empty points fallback
    pid::PidGains gNull = pid::GainScheduler::interpolate(150.0f, nullptr, 0, fallback);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, gNull.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.05f, gNull.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, gNull.kd);

    std::array<config::PidPoint, 3> points = {config::PidPoint{100.0f, 1.0f, 0.10f, 0.50f},
                                              config::PidPoint{150.0f, 2.0f, 0.20f, 1.00f},
                                              config::PidPoint{200.0f, 4.0f, 0.40f, 2.00f}};

    // 2. Below minimum point clamps to points[0]
    pid::PidGains gBelow = pid::GainScheduler::interpolate(50.0f, points.data(), points.size(), fallback);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, gBelow.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.10f, gBelow.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.50f, gBelow.kd);

    // 3. Above maximum point clamps to points[last]
    pid::PidGains gAbove = pid::GainScheduler::interpolate(250.0f, points.data(), points.size(), fallback);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, gAbove.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.40f, gAbove.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.00f, gAbove.kd);

    // 4. Exact match on interior support point
    pid::PidGains gExact = pid::GainScheduler::interpolate(150.0f, points.data(), points.size(), fallback);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, gExact.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.20f, gExact.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.00f, gExact.kd);

    // 5. Halfway interpolation between 100°C and 150°C (at 125°C)
    pid::PidGains gMid1 = pid::GainScheduler::interpolate(125.0f, points.data(), points.size(), fallback);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, gMid1.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.15f, gMid1.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, gMid1.kd);

    // 6. 75% interpolation between 150°C and 200°C (at 187.5°C)
    pid::PidGains gMid2 = pid::GainScheduler::interpolate(187.5f, points.data(), points.size(), fallback);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.5f, gMid2.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.35f, gMid2.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.75f, gMid2.kd);

    // 7. Degenerate deltaT <= 0.001f in interpolatePair returns p1
    config::PidPoint pA{150.0f, 2.0f, 0.2f, 1.0f};
    config::PidPoint pB{150.0f, 3.0f, 0.3f, 1.5f};
    pid::PidGains gDegen = pid::GainScheduler::interpolatePair(150.0f, pA, pB);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, gDegen.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, gDegen.ki);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, gDegen.kd);
}

// 15. PID Infinity Safety Test
static void test_pid_inf_safety() {
    pid::PIDController pid(2.0f, 0.5f, 1.0f);
    pid.begin();
    float posInf = std::numeric_limits<float>::infinity();
    float negInf = -std::numeric_limits<float>::infinity();

    // Verify setTunings rejects Infinity
    pid.setTunings(posInf, 0.5f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, pid.getKp());
    pid.setTunings(2.0f, negInf, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, pid.getKi());
    pid.setTunings(2.0f, 0.5f, posInf);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, pid.getKd());

    // Verify setSetpoint & setInput reject Infinity
    pid.setSetpoint(150.0f);
    pid.setSetpoint(posInf);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, pid.getSetpoint());

    pid.setInput(100.0f);
    pid.setInput(negInf);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, pid.getInput());

    // Verify setManualOutput rejects Infinity
    pid.setManualOutput(posInf);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, pid.getOutput());
}

// 16. Negative Tuning Gains Rejection Test
static void test_pid_negative_tunings_rejection() {
    pid::PIDController pid(2.0f, 0.5f, 1.0f);
    pid.begin();

    // Reject negative Kp
    pid.setTunings(-1.0f, 0.5f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, pid.getKp());

    // Reject negative Ki
    pid.setTunings(2.0f, -0.1f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, pid.getKi());

    // Reject negative Kd
    pid.setTunings(2.0f, 0.5f, -0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, pid.getKd());
}

// 17. GainScheduler NaN and Infinity Safety Test
static void test_pid_gain_scheduler_nan_inf_safety() {
    std::array<config::PidPoint, 2> pts = {{
        {100.0f, 1.0f, 0.1f, 0.5f},
        {200.0f, 2.0f, 0.2f, 1.0f},
    }};
    pid::PidGains fallback{3.0f, 0.3f, 1.5f};

    float qnan = std::numeric_limits<float>::quiet_NaN();
    float posInf = std::numeric_limits<float>::infinity();

    // NaN targetTemp returns fallback
    pid::PidGains gNan = pid::GainScheduler::interpolate(qnan, pts.data(), pts.size(), fallback);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, fallback.kp, gNan.kp);

    // Inf targetTemp returns fallback
    pid::PidGains gInf = pid::GainScheduler::interpolate(posInf, pts.data(), pts.size(), fallback);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, fallback.kp, gInf.kp);

    // interpolatePair with NaN or Inf returns p1
    pid::PidGains gPairNan = pid::GainScheduler::interpolatePair(qnan, pts[0], pts[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, pts[0].kp, gPairNan.kp);

    pid::PidGains gPairInf = pid::GainScheduler::interpolatePair(posInf, pts[0], pts[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, pts[0].kp, gPairInf.kp);
}

// 18. QuickPID Constructors & Defaults Test
static void test_quickpid_constructors_and_defaults() {
    // Default constructor
    QuickPID pidDefault;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::Control::manual), pidDefault.GetMode());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pidDefault.GetOutputSum());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pidDefault.GetKp());
    TEST_ASSERT_FALSE(pidDefault.Compute());

    // Simplified 3-argument constructor
    float in = 20.0f;
    float out = 0.0f;
    float sp = 50.0f;
    QuickPID pid3(&in, &out, &sp);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::Control::manual), pid3.GetMode());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::Action::direct), pid3.GetDirection());

    // 7-argument constructor
    QuickPID pid7(&in, &out, &sp, 2.0f, 0.5f, 0.1f, QuickPID::Action::reverse);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, pid7.GetKp());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, pid7.GetKi());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, pid7.GetKd());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::Action::reverse), pid7.GetDirection());

    // Full 10-argument constructor
    QuickPID pid10(&in, &out, &sp, 3.0f, 0.2f, 0.4f, QuickPID::pMode::pOnError, QuickPID::dMode::dOnError,
                   QuickPID::iAwMode::iAwOff, QuickPID::Action::direct);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::pMode::pOnError), pid10.GetPmode());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::dMode::dOnError), pid10.GetDmode());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::iAwMode::iAwOff), pid10.GetAwMode());
}

// 19. QuickPID Mode Transitions and Toggle Test
static void test_quickpid_modes_and_toggle() {
    float in = 100.0f;
    float out = 40.0f;
    float sp = 150.0f;
    QuickPID pid(&in, &out, &sp, 2.0f, 0.1f, 0.5f, QuickPID::Action::direct);

    // Initial mode is manual
    TEST_ASSERT_EQUAL_UINT8(0, pid.GetMode());

    // Toggle: manual -> automatic (bumpless transfer syncs outputSum from out)
    pid.SetMode(QuickPID::Control::toggle);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::Control::automatic), pid.GetMode());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, pid.GetOutputSum());

    // Toggle again: automatic -> manual
    pid.SetMode(QuickPID::Control::toggle);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::Control::manual), pid.GetMode());

    // uint8_t overload
    pid.SetMode(static_cast<uint8_t>(1)); // automatic
    TEST_ASSERT_EQUAL_UINT8(1, pid.GetMode());
    pid.SetMode(static_cast<uint8_t>(2)); // timer
    TEST_ASSERT_EQUAL_UINT8(2, pid.GetMode());
    pid.SetMode(static_cast<uint8_t>(3)); // toggle from timer -> manual
    TEST_ASSERT_EQUAL_UINT8(0, pid.GetMode());

    // Out-of-bounds mode integer rejected
    pid.SetMode(static_cast<uint8_t>(99));
    TEST_ASSERT_EQUAL_UINT8(0, pid.GetMode());
}

// 20. QuickPID Enum Configuration and Integer Setters Test
static void test_quickpid_enum_setters_and_bounds() {
    float in = 25.0f;
    float out = 0.0f;
    float sp = 100.0f;
    QuickPID pid(&in, &out, &sp, 1.0f, 0.1f, 0.1f, QuickPID::Action::direct);

    // Controller direction
    pid.SetControllerDirection(QuickPID::Action::reverse);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::Action::reverse), pid.GetDirection());
    pid.SetControllerDirection(static_cast<uint8_t>(0)); // direct
    TEST_ASSERT_EQUAL_UINT8(0, pid.GetDirection());
    pid.SetControllerDirection(static_cast<uint8_t>(5)); // out of bounds, ignored
    TEST_ASSERT_EQUAL_UINT8(0, pid.GetDirection());

    // Proportional mode
    pid.SetProportionalMode(QuickPID::pMode::pOnMeas);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::pMode::pOnMeas), pid.GetPmode());
    pid.SetProportionalMode(QuickPID::pMode::pOnErrorMeas);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::pMode::pOnErrorMeas), pid.GetPmode());
    pid.SetProportionalMode(static_cast<uint8_t>(0));  // pOnError
    TEST_ASSERT_EQUAL_UINT8(0, pid.GetPmode());
    pid.SetProportionalMode(static_cast<uint8_t>(10)); // out of bounds, ignored
    TEST_ASSERT_EQUAL_UINT8(0, pid.GetPmode());

    // Derivative mode
    pid.SetDerivativeMode(QuickPID::dMode::dOnError);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::dMode::dOnError), pid.GetDmode());
    pid.SetDerivativeMode(static_cast<uint8_t>(1)); // dOnMeas
    TEST_ASSERT_EQUAL_UINT8(1, pid.GetDmode());
    pid.SetDerivativeMode(static_cast<uint8_t>(7)); // out of bounds, ignored
    TEST_ASSERT_EQUAL_UINT8(1, pid.GetDmode());

    // Anti-windup mode
    pid.SetAntiWindupMode(QuickPID::iAwMode::iAwClamp);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::iAwMode::iAwClamp), pid.GetAwMode());
    pid.SetAntiWindupMode(QuickPID::iAwMode::iAwOff);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(QuickPID::iAwMode::iAwOff), pid.GetAwMode());
    pid.SetAntiWindupMode(static_cast<uint8_t>(0)); // iAwCondition
    TEST_ASSERT_EQUAL_UINT8(0, pid.GetAwMode());
    pid.SetAntiWindupMode(static_cast<uint8_t>(8)); // out of bounds, ignored
    TEST_ASSERT_EQUAL_UINT8(0, pid.GetAwMode());
}

// 21. QuickPID Sample Time Scaling Test
static void test_quickpid_sample_time_scaling() {
    float in = 20.0f;
    float out = 0.0f;
    float sp = 100.0f;
    QuickPID pid(&in, &out, &sp, 2.0f, 1.0f, 0.5f, QuickPID::Action::direct);

    // Initial sample time is 100,000 µs (0.1 s)
    // Scale sample time to 200,000 µs (ratio = 2.0)
    pid.SetSampleTimeUs(200000);

    // Passing 0 or identical sample time must be a no-op
    pid.SetSampleTimeUs(0);
    pid.SetSampleTimeUs(200000);

    // Compute in timer mode should work normally
    pid.SetMode(QuickPID::Control::timer);
    TEST_ASSERT_TRUE(pid.Compute());
}

// 22. QuickPID Calculation Modes (pOnError vs pOnErrorMeas, dOnError, Action::reverse)
static void test_quickpid_calculation_modes() {
    float in = 100.0f;
    float out = 0.0f;
    float sp = 150.0f;

    // 1. pOnError mode: P-term directly proportional to setpoint error (50 * 2.0 = 100)
    QuickPID pidErr(&in, &out, &sp, 2.0f, 0.0f, 0.0f, QuickPID::pMode::pOnError, QuickPID::dMode::dOnMeas,
                    QuickPID::iAwMode::iAwClamp, QuickPID::Action::direct);
    pidErr.SetMode(QuickPID::Control::timer);
    TEST_ASSERT_TRUE(pidErr.Compute());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, pidErr.GetPterm());

    // 2. pOnErrorMeas mode: split 50/50 error and measurement
    QuickPID pidErrMeas(&in, &out, &sp, 2.0f, 0.0f, 0.0f, QuickPID::pMode::pOnErrorMeas, QuickPID::dMode::dOnMeas,
                        QuickPID::iAwMode::iAwClamp, QuickPID::Action::direct);
    pidErrMeas.SetMode(QuickPID::Control::timer);
    TEST_ASSERT_TRUE(pidErrMeas.Compute());
    // Initial dInput is 0 (lastInput synced), so peTerm*0.5 = 50.0f
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, pidErrMeas.GetPterm());

    // 3. dOnError mode: derivative responds to setpoint changes
    QuickPID pidDerror(&in, &out, &sp, 0.0f, 0.0f, 1.0f, QuickPID::pMode::pOnError, QuickPID::dMode::dOnError,
                       QuickPID::iAwMode::iAwClamp, QuickPID::Action::direct);
    pidDerror.SetMode(QuickPID::Control::timer);
    TEST_ASSERT_TRUE(pidDerror.Compute()); // Establish baseline error = 50
    sp = 170.0f;                           // dError = +20
    TEST_ASSERT_TRUE(pidDerror.Compute());
    TEST_ASSERT_TRUE(pidDerror.GetDterm() > 0.0f);

    // 4. Reverse action (cooling)
    QuickPID pidRev(&in, &out, &sp, 2.0f, 0.1f, 0.0f, QuickPID::pMode::pOnError, QuickPID::dMode::dOnMeas,
                    QuickPID::iAwMode::iAwClamp, QuickPID::Action::reverse);
    pidRev.SetMode(QuickPID::Control::timer);
    // Process is 100°C, setpoint is 170°C -> for cooling, error is negative
    TEST_ASSERT_TRUE(pidRev.Compute());
    TEST_ASSERT_TRUE(pidRev.GetPterm() < 0.0f);
}

// 23. QuickPID Anti-Windup Modes (iAwCondition and iAwOff)
static void test_quickpid_anti_windup_modes() {
    float in = 20.0f;
    float out = 0.0f;
    float sp = 200.0f;

    // 1. iAwCondition mode: checks saturation and condition
    QuickPID pidCond(&in, &out, &sp, 2.0f, 0.5f, 0.1f, QuickPID::pMode::pOnError, QuickPID::dMode::dOnMeas,
                     QuickPID::iAwMode::iAwCondition, QuickPID::Action::direct);
    pidCond.SetMode(QuickPID::Control::timer);
    pidCond.SetOutputLimits(0.0f, 100.0f);

    for (int i = 0; i < 20; ++i) {
        TEST_ASSERT_TRUE(pidCond.Compute());
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, out);
    TEST_ASSERT_TRUE(pidCond.GetOutputSum() <= 100.01f);

    // 2. iAwOff mode: integral accumulator unbounded
    QuickPID pidOff(&in, &out, &sp, 1.0f, 1.0f, 0.0f, QuickPID::pMode::pOnError, QuickPID::dMode::dOnMeas,
                    QuickPID::iAwMode::iAwOff, QuickPID::Action::direct);
    pidOff.SetMode(QuickPID::Control::timer);
    pidOff.SetOutputLimits(0.0f, 100.0f);

    for (int i = 0; i < 30; ++i) {
        TEST_ASSERT_TRUE(pidOff.Compute());
    }
    // Output is clamped, but outputSum is allowed to grow beyond 100 in iAwOff
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, out);
    TEST_ASSERT_TRUE(pidOff.GetOutputSum() > 100.0f);
}

// 24. QuickPID Defensive Hardening (Nullptr, NaN, Inf, Min >= Max)
static void test_quickpid_defensive_hardening() {
    float in = 50.0f;
    float out = 25.0f;
    float sp = 100.0f;
    float qnan = std::numeric_limits<float>::quiet_NaN();
    float posInf = std::numeric_limits<float>::infinity();

    // 1. Nullptr inputs in Compute()
    QuickPID pidNull(nullptr, nullptr, nullptr);
    pidNull.SetMode(QuickPID::Control::timer);
    TEST_ASSERT_FALSE(pidNull.Compute());

    QuickPID pid(&in, &out, &sp, 2.0f, 0.5f, 0.1f, QuickPID::Action::direct);
    pid.SetMode(QuickPID::Control::timer);
    pid.SetOutputLimits(0.0f, 100.0f);

    // 2. NaN / Inf inputs in Compute()
    in = qnan;
    TEST_ASSERT_FALSE(pid.Compute());
    TEST_ASSERT_TRUE(std::isnan(in));
    in = 50.0f;
    sp = posInf;
    TEST_ASSERT_FALSE(pid.Compute());
    TEST_ASSERT_TRUE(std::isinf(sp));
    sp = 100.0f;
    TEST_ASSERT_TRUE(pid.Compute());

    // 3. SetTunings() rejects NaN, Inf, and negative
    pid.SetTunings(qnan, 0.5f, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, pid.GetKp());
    pid.SetTunings(2.0f, posInf, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, pid.GetKi());
    pid.SetTunings(2.0f, 0.5f, -1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, pid.GetKd());

    // 4. SetOutputLimits() rejects Min >= Max, NaN, Inf
    pid.SetOutputLimits(100.0f, 50.0f);
    pid.SetOutputLimits(50.0f, 50.0f);
    pid.SetOutputLimits(qnan, 100.0f);
    pid.SetOutputLimits(0.0f, posInf);

    // 5. SetOutputSum() rejects NaN / Inf and clamps within limits
    pid.SetOutputSum(qnan);
    TEST_ASSERT_FALSE(std::isnan(pid.GetOutputSum()));
    pid.SetOutputSum(150.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, pid.GetOutputSum());

    // 6. Ki == 0.0f clears outputSum
    pid.SetTunings(2.0f, 0.0f, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pid.GetOutputSum());
}

// 25. QuickPID Automatic Timing and Reset Test
static void test_quickpid_timing_and_reset() {
    float in = 50.0f;
    float out = 0.0f;
    float sp = 100.0f;
    QuickPID pid(&in, &out, &sp, 2.0f, 0.1f, 0.1f, QuickPID::Action::direct);
    pid.SetSampleTimeUs(50000); // 50 ms
    pid.SetMode(QuickPID::Control::automatic);

    // First compute executes
    TEST_ASSERT_TRUE(pid.Compute());

    // Immediate second call should return false due to sampleTimeUs throttle
    TEST_ASSERT_FALSE(pid.Compute());

    // Reset clears state cleanly
    pid.Reset();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pid.GetOutputSum());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pid.GetPterm());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pid.GetIterm());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pid.GetDterm());
}

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_pid_tests() {
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
    RUN_TEST(test_pid_nan_safety);
    RUN_TEST(test_pid_gain_scheduler_interpolation);
    RUN_TEST(test_pid_inf_safety);
    RUN_TEST(test_pid_negative_tunings_rejection);
    RUN_TEST(test_pid_gain_scheduler_nan_inf_safety);
    RUN_TEST(test_quickpid_constructors_and_defaults);
    RUN_TEST(test_quickpid_modes_and_toggle);
    RUN_TEST(test_quickpid_enum_setters_and_bounds);
    RUN_TEST(test_quickpid_sample_time_scaling);
    RUN_TEST(test_quickpid_calculation_modes);
    RUN_TEST(test_quickpid_anti_windup_modes);
    RUN_TEST(test_quickpid_defensive_hardening);
    RUN_TEST(test_quickpid_timing_and_reset);
}
