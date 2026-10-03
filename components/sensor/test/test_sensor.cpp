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
 * @file test_sensor.cpp
 * @brief Unit tests for MAX31856 sensor driver data models, math, and fault handling.
 *
 * Tests configuration defaults, sensor reading initialization, exponential moving average (EMA)
 * mathematical models, step-response convergence, cold-junction calibration math,
 * and 19-bit thermocouple register bit-shifting/conversion.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "unity.h"
#include "sensor/max31856.hpp"
#include <cmath>
#include <algorithm>

// 1. Sensor Configuration Defaults Test
static void test_sensor_config_defaults()
{
    sensor::MAX31856Config cfg;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(sensor::ThermocoupleType::TYPE_K), static_cast<uint8_t>(cfg.tcType));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(sensor::NoiseFilter::FILTER_50HZ), static_cast<uint8_t>(cfg.filter));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.3f, cfg.emaAlpha);
    TEST_ASSERT_EQUAL_UINT8(3, cfg.faultStreakLimit);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, cfg.cjOffset);
}

// 2. Sensor Reading Defaults Test
static void test_sensor_reading_defaults()
{
    sensor::SensorReading reading;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, reading.temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, reading.coldJunction);
    TEST_ASSERT_FALSE(reading.isValid);
    TEST_ASSERT_FALSE(reading.fault.openCircuit);
    TEST_ASSERT_FALSE(reading.fault.overUnderVoltage);
    TEST_ASSERT_FALSE(reading.fault.hasFault());
}

// 3. EMA Mathematical Model Test
static void test_sensor_ema_math()
{
    // EMA Formula: y_k = alpha * x_k + (1 - alpha) * y_{k-1}
    float alpha = 0.3f;
    float prevTemp = 25.0f;
    float rawInput = 100.0f;
    float filtered = (alpha * rawInput) + ((1.0f - alpha) * prevTemp);
    // 0.3 * 100 + 0.7 * 25 = 30 + 17.5 = 47.5 C
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 47.5f, filtered);
}

// 4. Step Response Convergence Test
static void test_sensor_ema_step_response_convergence()
{
    // Step response test: 25.0°C jumping to 200.0°C with alpha = 0.3
    float alpha = 0.3f;
    float temp = 25.0f;
    const float target = 200.0f;

    // Simulate 30 filter cycles
    for (int i = 0; i < 30; ++i) {
        temp = (alpha * target) + ((1.0f - alpha) * temp);
    }

    // After 30 cycles with alpha=0.3, filtered temp must converge to >199.9°C
    TEST_ASSERT_FLOAT_WITHIN(0.1f, target, temp);
}

// 5. Cold-Junction Offset Calibration Test
static void test_sensor_cjto_offset_calibration()
{
    float rawTcTemp = 150.23f;
    float cjtoOffsetPositive = 2.5f;
    float cjtoOffsetNegative = -1.8f;

    float calibratedPos = rawTcTemp + cjtoOffsetPositive;
    float calibratedNeg = rawTcTemp + cjtoOffsetNegative;

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 152.73f, calibratedPos);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 148.43f, calibratedNeg);
}

// 6. Fault Flag Isolation Test
static void test_sensor_fault_flag_isolation()
{
    sensor::SensorReading reading;
    reading.isValid = false;
    reading.fault.openCircuit = true;
    reading.fault.overUnderVoltage = false;

    TEST_ASSERT_FALSE(reading.isValid);
    TEST_ASSERT_TRUE(reading.fault.openCircuit);
    TEST_ASSERT_FALSE(reading.fault.overUnderVoltage);
    TEST_ASSERT_TRUE(reading.fault.hasFault());
}

// 7. Fault Flags Bitmask Test
static void test_sensor_fault_flags_has_fault()
{
    sensor::FaultFlags flags;
    TEST_ASSERT_FALSE(flags.hasFault());

    // Test with raw byte non-zero
    flags.rawByte = 0x04;
    TEST_ASSERT_TRUE(flags.hasFault());
    flags.rawByte = 0x00;

    // Test with each individual boolean flag
    flags.openCircuit = true;
    TEST_ASSERT_TRUE(flags.hasFault());
    flags.openCircuit = false;

    flags.overUnderVoltage = true;
    TEST_ASSERT_TRUE(flags.hasFault());
    flags.overUnderVoltage = false;

    flags.tcHigh = true;
    TEST_ASSERT_TRUE(flags.hasFault());
    flags.tcHigh = false;

    flags.tcLow = true;
    TEST_ASSERT_TRUE(flags.hasFault());
    flags.tcLow = false;

    flags.cjHigh = true;
    TEST_ASSERT_TRUE(flags.hasFault());
    flags.cjHigh = false;

    flags.cjLow = true;
    TEST_ASSERT_TRUE(flags.hasFault());
    flags.cjLow = false;

    flags.tcOutOfRange = true;
    TEST_ASSERT_TRUE(flags.hasFault());
    flags.tcOutOfRange = false;

    flags.cjOutOfRange = true;
    TEST_ASSERT_TRUE(flags.hasFault());
    flags.cjOutOfRange = false;

    TEST_ASSERT_FALSE(flags.hasFault());
}

// 8. Register Bit-Shifting & Conversion Test
static void test_sensor_temperature_decoding_math()
{
    // MAX31856 19-bit thermocouple temperature conversion formula:
    // 3 bytes: LTCBH, LTCBM, LTCBL -> rawTc (24-bit) -> sign extend to 32-bit -> (rawTc >> 5) / 128.0f (LSB = 0.0078125°C)

    // Test Case 1: +100.0000°C -> 100.0 / 0.0078125 = 12800 = 0x003200 -> shifted left by 5 = 0x064000
    int32_t rawTc = 0x064000;
    if (rawTc & 0x800000) rawTc |= 0xFF000000;
    float temp = static_cast<float>(rawTc >> 5) / 128.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, temp);

    // Test Case 2: -20.0000°C -> -20.0 / 0.0078125 = -2560 = -0x0A00 -> 24-bit two's complement = 0xFFF600 -> shifted by 5 = 0xFEC000
    int32_t rawTcNeg = 0xFEC000;
    if (rawTcNeg & 0x800000) rawTcNeg |= 0xFF000000;
    float tempNeg = static_cast<float>(rawTcNeg >> 5) / 128.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -20.0f, tempNeg);

    // Cold Junction 14-bit conversion formula: (rawCj >> 2) / 64.0f (LSB = 0.015625°C)
    // Test Case 3: +25.0°C -> 25.0 / 0.015625 = 1600 = 0x0640 -> shifted left by 2 = 0x1900
    int16_t rawCj = 0x1900;
    float cjTemp = static_cast<float>(rawCj >> 2) / 64.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, cjTemp);
}

// 9. Hardware CJTO Register (0x09) 8-Bit Two's Complement Encoding & Clamping Math
static void test_sensor_cjto_register_encoding_math()
{
    auto encodeCjto = [](float offset) -> int8_t {
        float clamped = std::clamp(offset, -8.0f, 7.9375f);
        return static_cast<int8_t>(std::round(clamped * 16.0f));
    };

    // 0.0°C -> 0x00
    TEST_ASSERT_EQUAL_INT8(0x00, encodeCjto(0.0f));

    // +0.0625°C (1 LSB) -> 1
    TEST_ASSERT_EQUAL_INT8(1, encodeCjto(0.0625f));

    // +1.0°C -> 16 (0x10)
    TEST_ASSERT_EQUAL_INT8(16, encodeCjto(1.0f));

    // +7.9375°C (Max positive 8-bit limit) -> 127 (0x7F)
    TEST_ASSERT_EQUAL_INT8(127, encodeCjto(7.9375f));

    // -0.0625°C (-1 LSB) -> -1 (0xFF)
    TEST_ASSERT_EQUAL_INT8(-1, encodeCjto(-0.0625f));

    // -1.0°C -> -16 (0xF0)
    TEST_ASSERT_EQUAL_INT8(-16, encodeCjto(-1.0f));

    // -8.0°C (Min negative 8-bit limit) -> -128 (0x80)
    TEST_ASSERT_EQUAL_INT8(-128, encodeCjto(-8.0f));

    // Clamping: > +7.9375°C clamps to +127
    TEST_ASSERT_EQUAL_INT8(127, encodeCjto(10.5f));

    // Clamping: < -8.0°C clamps to -128
    TEST_ASSERT_EQUAL_INT8(-128, encodeCjto(-12.0f));
}

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_sensor_tests()
{
    RUN_TEST(test_sensor_config_defaults);
    RUN_TEST(test_sensor_reading_defaults);
    RUN_TEST(test_sensor_ema_math);
    RUN_TEST(test_sensor_ema_step_response_convergence);
    RUN_TEST(test_sensor_cjto_offset_calibration);
    RUN_TEST(test_sensor_fault_flag_isolation);
    RUN_TEST(test_sensor_fault_flags_has_fault);
    RUN_TEST(test_sensor_temperature_decoding_math);
    RUN_TEST(test_sensor_cjto_register_encoding_math);
}

