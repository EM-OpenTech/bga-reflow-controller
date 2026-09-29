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
 * @file test_safety.cpp
 * @brief Unit tests for hardware and thermal safety watchdog component.
 *
 * Tests fault detection for over-temperature, under-temperature, sensor disconnects,
 * MAX31856 hardware IC flags, stuck SSRs, heater failure (no-rise), reset cycles,
 * and hardware inhibition triggering.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "unity.h"
#include "safety/safety_watchdog.hpp"
#include "output/output_manager.hpp"
#include <cmath>

static void test_safety_clean()
{
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);
    TEST_ASSERT_FALSE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::NONE), static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_FALSE(outputs.isInhibited());
}

static void test_safety_overtemp()
{
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = 295.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 100.0f;
    botReading.isValid = true;

    bool faultDetected = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::OVERTEMP_TOP), static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

static void test_safety_bottom_overtemp()
{
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = 150.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 285.0f;
    botReading.isValid = true;

    bool faultDetected = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::OVERTEMP_BOTTOM), static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

static void test_safety_undertemp()
{
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = 2.0f; // Below default 5.0°C
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 25.0f;
    botReading.isValid = true;

    bool faultDetected = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::UNDERTEMP_TOP), static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());

    // Test with min temp check disabled
    watchdog.reset();
    safety::SafetyConfig cfg = watchdog.getConfig();
    cfg.enableMinTempCheck = false;
    watchdog.updateConfig(cfg);

    faultDetected = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_FALSE(faultDetected);
    TEST_ASSERT_FALSE(watchdog.hasFault());
    TEST_ASSERT_FALSE(outputs.isInhibited());
}

static void test_safety_disconnect()
{
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = NAN;
    topReading.isValid = false;
    topReading.fault.openCircuit = true;
    sensor::SensorReading botReading;
    botReading.temperature = 100.0f;
    botReading.isValid = true;

    bool faultDetected = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::SENSOR_FAULT_TOP), static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

static void test_safety_ic_hardware_fault()
{
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = 100.0f;
    topReading.isValid = true;
    topReading.fault.openCircuit = true; // IC hardware fault flag
    topReading.fault.rawByte = 0x01;
    sensor::SensorReading botReading;
    botReading.temperature = 100.0f;
    botReading.isValid = true;

    bool faultDetected = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::HARDWARE_IC_FAULT_TOP), static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

static void test_safety_stuck_ssr()
{
    output::OutputManager outputs;
    safety::SafetyConfig cfg;
    cfg.stuckSsrWindowSec = 0; // Immediate evaluation for testing
    cfg.stuckSsrRiseThreshold = 2.0f;
    safety::SafetyWatchdog watchdog(outputs, cfg);

    sensor::SensorReading topReading;
    topReading.temperature = 100.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 50.0f;
    botReading.isValid = true;

    // First check activates stuck SSR monitoring (power = 0%, temp > 50°C)
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_FALSE(watchdog.hasFault());

    // Second check with temp rising 5°C (> 2°C threshold) while power is 0%
    topReading.temperature = 105.0f;
    bool faultDetected = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::STUCK_SSR_TOP), static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

static void test_safety_no_rise()
{
    output::OutputManager outputs;
    safety::SafetyConfig cfg;
    cfg.noRiseTimeoutSec = 0; // Immediate timeout for testing
    cfg.noRiseThreshold = 3.0f;
    safety::SafetyWatchdog watchdog(outputs, cfg);

    sensor::SensorReading topReading;
    topReading.temperature = 60.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 50.0f;
    botReading.isValid = true;

    // Power 100%, setpoint 200°C (far from target: 200 - 60 = 140°C > 20°C)
    watchdog.check(topReading, botReading, 100.0f, 0.0f, 200.0f, 0.0f);
    TEST_ASSERT_FALSE(watchdog.hasFault());

    // Next check: temp remained 60.0f (< 3.0f rise) -> trigger NO_RISE_TOP
    bool faultDetected = watchdog.check(topReading, botReading, 100.0f, 0.0f, 200.0f, 0.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::NO_RISE_TOP), static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

static void test_safety_reset()
{
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = 295.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 25.0f;
    botReading.isValid = true;

    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(watchdog.hasFault());

    watchdog.reset();
    TEST_ASSERT_FALSE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::NONE), static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_FALSE(outputs.isInhibited());
}

static void test_safety_fault_strings()
{
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    TEST_ASSERT_NOT_NULL(watchdog.getFaultString());
    TEST_ASSERT_EQUAL_STRING("No Fault", watchdog.getFaultString());
}

void run_safety_tests()
{
    RUN_TEST(test_safety_clean);
    RUN_TEST(test_safety_overtemp);
    RUN_TEST(test_safety_bottom_overtemp);
    RUN_TEST(test_safety_undertemp);
    RUN_TEST(test_safety_disconnect);
    RUN_TEST(test_safety_ic_hardware_fault);
    RUN_TEST(test_safety_stuck_ssr);
    RUN_TEST(test_safety_no_rise);
    RUN_TEST(test_safety_reset);
    RUN_TEST(test_safety_fault_strings);
}

