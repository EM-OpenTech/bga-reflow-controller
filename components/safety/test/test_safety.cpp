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

#include "output/output_manager.hpp"
#include "safety/safety_watchdog.hpp"
#include "unity.h"
#include <cmath>
#include <cstring>

// 1. Clean State Baseline Test
static void test_safety_clean() {
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);
    TEST_ASSERT_FALSE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::NONE), static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_FALSE(outputs.isInhibited());
}

// 2. Top Over-Temperature Fault Test
static void test_safety_overtemp() {
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::OVERTEMP_TOP),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 3. Bottom Over-Temperature Fault Test
static void test_safety_bottom_overtemp() {
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::OVERTEMP_BOTTOM),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 4. Under-Temperature Fault Test
static void test_safety_undertemp() {
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::UNDERTEMP_TOP),
                            static_cast<uint8_t>(watchdog.getFault()));
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

// 5. Sensor Disconnect Fault Test
static void test_safety_disconnect() {
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::SENSOR_FAULT_TOP),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 6. MAX31856 IC Hardware Fault Test
static void test_safety_ic_hardware_fault() {
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::HARDWARE_IC_FAULT_TOP),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 7. Stuck SSR Fault Test
static void test_safety_stuck_ssr() {
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::STUCK_SSR_TOP),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 8. Heater Failure (No-Rise) Test
static void test_safety_no_rise() {
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::NO_RISE_TOP),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 9. Safety Watchdog Reset Test
static void test_safety_reset() {
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

// 10. Fault String Representation Test
static void test_safety_fault_strings() {
    output::OutputManager outputs;
    safety::SafetyConfig cfg;
    cfg.stuckSsrWindowSec = 0;
    cfg.noRiseTimeoutSec = 0;
    safety::SafetyWatchdog watchdog(outputs, cfg);

    TEST_ASSERT_NOT_NULL(watchdog.getFaultString());
    TEST_ASSERT_EQUAL_STRING("No Fault", watchdog.getFaultString());

    // Trigger OVERTEMP_TOP
    sensor::SensorReading topReading;
    topReading.temperature = 295.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 100.0f;
    botReading.isValid = true;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Top heater overtemperature"));

    // Trigger OVERTEMP_BOTTOM
    watchdog.reset();
    topReading.temperature = 100.0f;
    botReading.temperature = 295.0f;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Bottom heater overtemperature"));

    // Trigger UNDERTEMP_TOP
    watchdog.reset();
    topReading.temperature = 2.0f;
    botReading.temperature = 100.0f;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Top heater undertemperature"));

    // Trigger UNDERTEMP_BOTTOM
    watchdog.reset();
    topReading.temperature = 100.0f;
    botReading.temperature = 2.0f;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Bottom heater undertemperature"));

    // Trigger SENSOR_FAULT_TOP
    watchdog.reset();
    topReading.temperature = 100.0f;
    topReading.isValid = false;
    botReading.temperature = 100.0f;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Top sensor"));

    // Trigger SENSOR_FAULT_BOTTOM
    watchdog.reset();
    topReading.isValid = true;
    botReading.isValid = false;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Bottom sensor"));

    // Trigger HARDWARE_IC_FAULT_TOP
    watchdog.reset();
    botReading.isValid = true;
    topReading.fault.openCircuit = true;
    topReading.fault.rawByte = 0x01;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Top MAX31856 IC"));

    // Trigger HARDWARE_IC_FAULT_BOTTOM
    watchdog.reset();
    topReading.fault.openCircuit = false;
    topReading.fault.rawByte = 0x00;
    botReading.fault.openCircuit = true;
    botReading.fault.rawByte = 0x01;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Bottom MAX31856 IC"));

    // Trigger STUCK_SSR_TOP
    watchdog.reset();
    botReading.fault.openCircuit = false;
    botReading.fault.rawByte = 0x00;
    topReading.temperature = 100.0f;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    topReading.temperature = 106.0f;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Top SSR stuck"));

    // Trigger STUCK_SSR_BOTTOM
    watchdog.reset();
    topReading.temperature = 100.0f;
    botReading.temperature = 100.0f;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    botReading.temperature = 106.0f;
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Bottom SSR stuck"));

    // Trigger NO_RISE_TOP
    watchdog.reset();
    topReading.temperature = 60.0f;
    botReading.temperature = 60.0f;
    watchdog.check(topReading, botReading, 100.0f, 0.0f, 200.0f, 0.0f);
    watchdog.check(topReading, botReading, 100.0f, 0.0f, 200.0f, 0.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Top heater failed"));

    // Trigger NO_RISE_BOTTOM
    watchdog.reset();
    topReading.temperature = 60.0f;
    botReading.temperature = 60.0f;
    watchdog.check(topReading, botReading, 0.0f, 100.0f, 0.0f, 200.0f);
    watchdog.check(topReading, botReading, 0.0f, 100.0f, 0.0f, 200.0f);
    TEST_ASSERT_NOT_NULL(strstr(watchdog.getFaultString(), "Bottom heater failed"));
}

// 11. Master Bypass Disabled Watchdog Test
static void test_safety_bypass_disabled() {
    output::OutputManager outputs;
    safety::SafetyConfig cfg;
    cfg.enableSafetyWatchdog = false;
    safety::SafetyWatchdog watchdog(outputs, cfg);

    // Provide invalid readings & extreme overtemp
    sensor::SensorReading topReading;
    topReading.temperature = 350.0f;
    topReading.isValid = false;
    sensor::SensorReading botReading;
    botReading.temperature = 350.0f;
    botReading.isValid = false;

    bool faultDetected = watchdog.check(topReading, botReading, 100.0f, 100.0f);
    TEST_ASSERT_FALSE(faultDetected);
    TEST_ASSERT_FALSE(watchdog.hasFault());
    TEST_ASSERT_FALSE(outputs.isInhibited());
}

// 12. Floating Point NaN / Inf Invalidation Protection Test
static void test_safety_nan_protection() {
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = NAN;
    topReading.isValid = true; // Claimed valid, but numeric value is NaN
    sensor::SensorReading botReading;
    botReading.temperature = 25.0f;
    botReading.isValid = true;

    bool fault = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(fault);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 13. Dual-Channel Concurrent Fault Prioritisation Test
static void test_safety_multi_channel_simultaneous_fault() {
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    // Both Top and Bottom report simultaneous critical overtemperatures (>280°C)
    sensor::SensorReading topReading;
    topReading.temperature = 295.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 290.0f;
    botReading.isValid = true;

    bool fault = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(fault);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 14. Bottom Under-Temperature Fault Test
static void test_safety_bottom_undertemp() {
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = 25.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 2.0f; // Below default 5.0°C
    botReading.isValid = true;

    bool faultDetected = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::UNDERTEMP_BOTTOM),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 15. Bottom Sensor Disconnect Fault Test
static void test_safety_bottom_disconnect() {
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = 100.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 100.0f;
    botReading.isValid = false; // Disconnected wire / SPI fail

    bool faultDetected = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::SENSOR_FAULT_BOTTOM),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 16. Bottom MAX31856 IC Hardware Fault Test
static void test_safety_bottom_ic_hardware_fault() {
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = 100.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 100.0f;
    botReading.isValid = true;
    botReading.fault.openCircuit = true;
    botReading.fault.rawByte = 0x01;

    bool faultDetected = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::HARDWARE_IC_FAULT_BOTTOM),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 17. Bottom Stuck SSR Fault Test
static void test_safety_bottom_stuck_ssr() {
    output::OutputManager outputs;
    safety::SafetyConfig cfg;
    cfg.stuckSsrWindowSec = 0; // Immediate evaluation for testing
    cfg.stuckSsrRiseThreshold = 2.0f;
    safety::SafetyWatchdog watchdog(outputs, cfg);

    sensor::SensorReading topReading;
    topReading.temperature = 50.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 100.0f;
    botReading.isValid = true;

    // First check activates stuck SSR monitoring on bottom
    watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_FALSE(watchdog.hasFault());

    // Second check with bottom temp rising 5°C (> 2°C threshold) while power is 0%
    botReading.temperature = 105.0f;
    bool faultDetected = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::STUCK_SSR_BOTTOM),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 18. Bottom Heater Failure (No-Rise) Test
static void test_safety_bottom_no_rise() {
    output::OutputManager outputs;
    safety::SafetyConfig cfg;
    cfg.noRiseTimeoutSec = 0; // Immediate timeout for testing
    cfg.noRiseThreshold = 3.0f;
    safety::SafetyWatchdog watchdog(outputs, cfg);

    sensor::SensorReading topReading;
    topReading.temperature = 50.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 60.0f;
    botReading.isValid = true;

    // Bottom power 100%, setpoint 200°C
    watchdog.check(topReading, botReading, 0.0f, 100.0f, 0.0f, 200.0f);
    TEST_ASSERT_FALSE(watchdog.hasFault());

    // Next check: bottom temp remained 60.0f (< 3.0f rise) -> trigger NO_RISE_BOTTOM
    bool faultDetected = watchdog.check(topReading, botReading, 0.0f, 100.0f, 0.0f, 200.0f);
    TEST_ASSERT_TRUE(faultDetected);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::NO_RISE_BOTTOM),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 19. Bottom Floating Point NaN / Inf Protection Test
static void test_safety_bottom_nan_protection() {
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = 25.0f;
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = NAN;
    botReading.isValid = true;

    bool fault = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(fault);
    TEST_ASSERT_TRUE(watchdog.hasFault());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::SENSOR_FAULT_BOTTOM),
                            static_cast<uint8_t>(watchdog.getFault()));
    TEST_ASSERT_TRUE(outputs.isInhibited());
}

// 20. Sustained Latched Fault Inhibition & Zero-Retrip Test
static void test_safety_sustained_fault_inhibit() {
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    sensor::SensorReading topReading;
    topReading.temperature = 295.0f; // Over-temperature
    topReading.isValid = true;
    sensor::SensorReading botReading;
    botReading.temperature = 100.0f;
    botReading.isValid = true;

    // 1. Initial fault trip
    bool initialTrip = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(initialTrip);
    TEST_ASSERT_TRUE(outputs.isInhibited());

    // 2. Second tick: temperatures have cooled to safe 100°C, but fault is latched
    topReading.temperature = 100.0f;
    bool sustainedTrip = watchdog.check(topReading, botReading, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(sustainedTrip);
    TEST_ASSERT_TRUE(outputs.isInhibited());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(safety::SafetyFault::OVERTEMP_TOP),
                            static_cast<uint8_t>(watchdog.getFault()));
}

// 21. Safety Configuration Validation & Rejection Test
static void test_safety_config_validation() {
    output::OutputManager outputs;
    safety::SafetyWatchdog watchdog(outputs);

    safety::SafetyConfig validCfg;
    TEST_ASSERT_TRUE(validCfg.validate());

    // Invalidate: max < min
    safety::SafetyConfig invalidCfg1 = validCfg;
    invalidCfg1.maxTempTop = 0.0f;
    invalidCfg1.minTempTop = 50.0f;
    TEST_ASSERT_FALSE(invalidCfg1.validate());

    // Invalidate: NaN value
    safety::SafetyConfig invalidCfg2 = validCfg;
    invalidCfg2.stuckSsrRiseThreshold = NAN;
    TEST_ASSERT_FALSE(invalidCfg2.validate());

    // Invalidate: non-positive thresholds
    safety::SafetyConfig invalidCfg3 = validCfg;
    invalidCfg3.noRiseThreshold = -1.0f;
    TEST_ASSERT_FALSE(invalidCfg3.validate());

    // Ensure updateConfig rejects invalid configurations and retains current
    watchdog.updateConfig(invalidCfg1);
    TEST_ASSERT_EQUAL_FLOAT(safety::DEFAULT_MAX_TEMP_TOP, watchdog.getConfig().maxTempTop);
}

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_safety_tests() {
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
    RUN_TEST(test_safety_bypass_disabled);
    RUN_TEST(test_safety_nan_protection);
    RUN_TEST(test_safety_multi_channel_simultaneous_fault);
    RUN_TEST(test_safety_bottom_undertemp);
    RUN_TEST(test_safety_bottom_disconnect);
    RUN_TEST(test_safety_bottom_ic_hardware_fault);
    RUN_TEST(test_safety_bottom_stuck_ssr);
    RUN_TEST(test_safety_bottom_no_rise);
    RUN_TEST(test_safety_bottom_nan_protection);
    RUN_TEST(test_safety_sustained_fault_inhibit);
    RUN_TEST(test_safety_config_validation);
}
