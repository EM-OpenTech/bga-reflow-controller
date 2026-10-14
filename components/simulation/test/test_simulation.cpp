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
 * @file test_simulation.cpp
 * @brief Unit tests for sim::ThermalSimulator.
 *
 * Tests cover:
 *  1. Initial conditions and reset with custom temperatures
 *  2. Top heater power ramp response and channel isolation
 *  3. Bottom heater power ramp response and channel isolation
 *  4. Forced convection fan cooling rate
 *  5. Temperature floor clamping at AMBIENT_TEMP (25°C)
 *  6. Simultaneous dual-zone heating with fan active
 *  7. NaN and Inf power input rejection
 *  8. Negative and excess power clamping
 *  9. Direct temperature override NaN and Inf protection
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "unity.h"
#include <cmath>
#include <limits>
#include "simulation/thermal_simulator.hpp"
#include "config/machine_config.hpp"

static constexpr uint32_t DT_MS = config::Timing::CONTROL_LOOP_PERIOD_MS;

// 1. Initial Conditions Test
static void test_sim_initial_conditions()
{
    sim::ThermalSimulator sim;
    sim.reset(sim::ThermalSimulator::AMBIENT_TEMP);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getTopTemperature());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getBottomTemperature());

    sim.reset(100.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, sim.getTopTemperature());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, sim.getBottomTemperature());
}

// 2. Top Heating Ramp Test
static void test_sim_top_heating_ramp()
{
    sim::ThermalSimulator sim;
    sim.reset(sim::ThermalSimulator::AMBIENT_TEMP);
    const int steps = static_cast<int>(5000 / DT_MS); // 5 seconds of heating
    for (int i = 0; i < steps; ++i) {
        sim.update(100.0f, 0.0f, false, DT_MS);
    }
    TEST_ASSERT_TRUE(sim.getTopTemperature() > 40.0f);
    TEST_ASSERT_TRUE(sim.getTopTemperature() < 45.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getBottomTemperature());
}

// 3. Bottom Heating Ramp Test
static void test_sim_bottom_heating_ramp()
{
    sim::ThermalSimulator sim;
    sim.reset(sim::ThermalSimulator::AMBIENT_TEMP);
    const int steps = static_cast<int>(10000 / DT_MS); // 10 seconds of heating
    for (int i = 0; i < steps; ++i) {
        sim.update(0.0f, 100.0f, false, DT_MS);
    }
    TEST_ASSERT_TRUE(sim.getBottomTemperature() > 37.0f);
    TEST_ASSERT_TRUE(sim.getBottomTemperature() < 42.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getTopTemperature());
}

// 4. Fan Cooling Test
static void test_sim_fan_cooling()
{
    sim::ThermalSimulator simNoFan;
    sim::ThermalSimulator simFan;
    simNoFan.setTopTemperature(200.0f);
    simFan.setTopTemperature(200.0f);

    const int steps = static_cast<int>(10000 / DT_MS); // 10 seconds of cooling
    for (int i = 0; i < steps; ++i) {
        simNoFan.update(0.0f, 0.0f, false, DT_MS);
        simFan.update(0.0f, 0.0f, true, DT_MS);
    }
    TEST_ASSERT_TRUE(simFan.getTopTemperature() < simNoFan.getTopTemperature());
    TEST_ASSERT_TRUE(simFan.getTopTemperature() < 175.0f);
}

// 5. Ambient Floor Clamping Test
static void test_sim_clamping()
{
    sim::ThermalSimulator sim;
    sim.reset(sim::ThermalSimulator::AMBIENT_TEMP);
    const int steps = static_cast<int>(10000 / DT_MS); // 10 seconds
    for (int i = 0; i < steps; ++i) {
        sim.update(0.0f, 0.0f, true, DT_MS);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getTopTemperature());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getBottomTemperature());
}

// 6. Simultaneous Heating & Fan Active Test
static void test_sim_simultaneous_heating_and_cooling()
{
    sim::ThermalSimulator sim;
    sim.reset(sim::ThermalSimulator::AMBIENT_TEMP);
    const int steps = static_cast<int>(5000 / DT_MS); // 5 seconds of dual heating
    for (int i = 0; i < steps; ++i) {
        sim.update(100.0f, 100.0f, true, DT_MS);
    }
    // Net rise should be positive even with fan active due to full heating power
    TEST_ASSERT_TRUE(sim.getTopTemperature() > sim::ThermalSimulator::AMBIENT_TEMP);
    TEST_ASSERT_TRUE(sim.getBottomTemperature() > sim::ThermalSimulator::AMBIENT_TEMP);
}

// 7. NaN and Inf Power Input Rejection Test
static void test_sim_nan_inf_power_protection()
{
    sim::ThermalSimulator sim;
    sim.reset(sim::ThermalSimulator::AMBIENT_TEMP);

    // Update with NaN and Inf power
    sim.update(std::numeric_limits<float>::quiet_NaN(), 50.0f, false, DT_MS);
    TEST_ASSERT_FALSE(std::isnan(sim.getTopTemperature()));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getTopTemperature());
    TEST_ASSERT_TRUE(sim.getBottomTemperature() > sim::ThermalSimulator::AMBIENT_TEMP);

    sim.update(50.0f, std::numeric_limits<float>::infinity(), false, DT_MS);
    TEST_ASSERT_FALSE(std::isinf(sim.getBottomTemperature()));
    TEST_ASSERT_FALSE(std::isnan(sim.getBottomTemperature()));
}

// 8. Negative and Excess Power Clamping Test
static void test_sim_power_clamping()
{
    sim::ThermalSimulator sim1;
    sim::ThermalSimulator sim2;
    sim1.reset(sim::ThermalSimulator::AMBIENT_TEMP);
    sim2.reset(sim::ThermalSimulator::AMBIENT_TEMP);

    // Negative power should clamp to 0.0f and not chill below ambient
    sim1.update(-50.0f, -100.0f, false, DT_MS);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim1.getTopTemperature());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim1.getBottomTemperature());

    // Excess power (150%) should produce identical rise as 100%
    sim1.update(150.0f, 200.0f, false, DT_MS);
    sim2.update(100.0f, 100.0f, false, DT_MS);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, sim2.getTopTemperature(), sim1.getTopTemperature());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, sim2.getBottomTemperature(), sim1.getBottomTemperature());
}

// 9. Direct Temperature Override NaN and Inf Protection Test
static void test_sim_nan_inf_override_protection()
{
    sim::ThermalSimulator sim;
    sim.reset(sim::ThermalSimulator::AMBIENT_TEMP);

    // Invalid reset should fallback to AMBIENT_TEMP
    sim.reset(std::numeric_limits<float>::quiet_NaN());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getTopTemperature());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getBottomTemperature());

    // Sub-ambient reset should clamp to AMBIENT_TEMP
    sim.reset(-10.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getTopTemperature());

    // Valid override
    sim.setTopTemperature(150.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, sim.getTopTemperature());

    // NaN / Inf override should be rejected and previous temperature preserved
    sim.setTopTemperature(std::numeric_limits<float>::quiet_NaN());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, sim.getTopTemperature());

    sim.setBottomTemperature(std::numeric_limits<float>::infinity());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getBottomTemperature());
}

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_simulation_tests()
{
    RUN_TEST(test_sim_initial_conditions);
    RUN_TEST(test_sim_top_heating_ramp);
    RUN_TEST(test_sim_bottom_heating_ramp);
    RUN_TEST(test_sim_fan_cooling);
    RUN_TEST(test_sim_clamping);
    RUN_TEST(test_sim_simultaneous_heating_and_cooling);
    RUN_TEST(test_sim_nan_inf_power_protection);
    RUN_TEST(test_sim_power_clamping);
    RUN_TEST(test_sim_nan_inf_override_protection);
}
