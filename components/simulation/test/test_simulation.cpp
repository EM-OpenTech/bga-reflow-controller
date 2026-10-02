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
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "unity.h"
#include "simulation/thermal_simulator.hpp"

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
    for (int i = 0; i < 50; ++i) {
        sim.update(100.0f, 0.0f, false, 100);
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
    for (int i = 0; i < 100; ++i) {
        sim.update(0.0f, 100.0f, false, 100);
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

    for (int i = 0; i < 100; ++i) {
        simNoFan.update(0.0f, 0.0f, false, 100);
        simFan.update(0.0f, 0.0f, true, 100);
    }
    TEST_ASSERT_TRUE(simFan.getTopTemperature() < simNoFan.getTopTemperature());
    TEST_ASSERT_TRUE(simFan.getTopTemperature() < 175.0f);
}

// 5. Ambient Floor Clamping Test
static void test_sim_clamping()
{
    sim::ThermalSimulator sim;
    sim.reset(sim::ThermalSimulator::AMBIENT_TEMP);
    for (int i = 0; i < 100; ++i) {
        sim.update(0.0f, 0.0f, true, 100);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getTopTemperature());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sim::ThermalSimulator::AMBIENT_TEMP, sim.getBottomTemperature());
}

// 6. Simultaneous Heating & Fan Active Test
static void test_sim_simultaneous_heating_and_cooling()
{
    sim::ThermalSimulator sim;
    sim.reset(sim::ThermalSimulator::AMBIENT_TEMP);
    for (int i = 0; i < 50; ++i) {
        sim.update(100.0f, 100.0f, true, 100);
    }
    // Net rise should be positive even with fan active due to full heating power
    TEST_ASSERT_TRUE(sim.getTopTemperature() > sim::ThermalSimulator::AMBIENT_TEMP);
    TEST_ASSERT_TRUE(sim.getBottomTemperature() > sim::ThermalSimulator::AMBIENT_TEMP);
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
}
