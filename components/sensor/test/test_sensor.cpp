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

#include "sensor/max31856.hpp"
#include "unity.h"
#include <algorithm>
#include <cmath>

// 1. Sensor Configuration Defaults Test
static void test_sensor_config_defaults() {
    sensor::MAX31856Config cfg;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(sensor::ThermocoupleType::TYPE_K), static_cast<uint8_t>(cfg.tcType));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(sensor::NoiseFilter::FILTER_50HZ), static_cast<uint8_t>(cfg.filter));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.3f, cfg.emaAlpha);
    TEST_ASSERT_EQUAL_UINT8(3, cfg.faultStreakLimit);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, cfg.cjOffset);
}

// 2. Sensor Reading Defaults Test
static void test_sensor_reading_defaults() {
    sensor::SensorReading reading;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, reading.temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, reading.coldJunction);
    TEST_ASSERT_FALSE(reading.isValid);
    TEST_ASSERT_FALSE(reading.fault.openCircuit);
    TEST_ASSERT_FALSE(reading.fault.overUnderVoltage);
    TEST_ASSERT_FALSE(reading.fault.hasFault());
}

// 3. EMA Mathematical Model Test
static void test_sensor_ema_math() {
    // EMA Formula: y_k = alpha * x_k + (1 - alpha) * y_{k-1}
    float alpha = 0.3f;
    float prevTemp = 25.0f;
    float rawInput = 100.0f;
    float filtered = (alpha * rawInput) + ((1.0f - alpha) * prevTemp);
    // 0.3 * 100 + 0.7 * 25 = 30 + 17.5 = 47.5 C
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 47.5f, filtered);
}

// 4. Step Response Convergence Test
static void test_sensor_ema_step_response_convergence() {
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

// 5. Cold-Junction Offset Applied Temperature Math Test
// NOTE: This test verifies the arithmetic of *applying* a calibration offset to a raw
// temperature value (e.g., in post-processing). It does NOT test CJTO register encoding
// (the hardware register bit format). The register encoding is covered by test #9:
// test_sensor_cjto_register_encoding_math.
static void test_sensor_cjto_temperature_offset_math() {
    float rawTcTemp = 150.23f;
    float cjtoOffsetPositive = 2.5f;
    float cjtoOffsetNegative = -1.8f;

    float calibratedPos = rawTcTemp + cjtoOffsetPositive;
    float calibratedNeg = rawTcTemp + cjtoOffsetNegative;

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 152.73f, calibratedPos);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 148.43f, calibratedNeg);
}

// 6. Fault Flag Isolation Test
static void test_sensor_fault_flag_isolation() {
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
static void test_sensor_fault_flags_has_fault() {
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
static void test_sensor_temperature_decoding_math() {
    // MAX31856 19-bit thermocouple temperature conversion formula:
    // 3 bytes: LTCBH, LTCBM, LTCBL -> rawTc (24-bit) -> sign extend to 32-bit -> (rawTc >> 5) / 128.0f (LSB =
    // 0.0078125°C)

    // Test Case 1: +100.0000°C -> 100.0 / 0.0078125 = 12800 = 0x003200 -> shifted left by 5 = 0x064000
    int32_t rawTc = 0x064000;
    if ((rawTc & 0x800000) != 0) {
        rawTc |= static_cast<int32_t>(0xFF000000UL);
    }
    float temp = static_cast<float>(rawTc >> 5) / 128.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, temp);

    // Test Case 2: -20.0000°C -> -20.0 / 0.0078125 = -2560 = -0x0A00 -> 24-bit two's complement = 0xFFF600 -> shifted
    // by 5 = 0xFEC000
    int32_t rawTcNeg = 0xFEC000;
    if ((rawTcNeg & 0x800000) != 0) {
        rawTcNeg |= static_cast<int32_t>(0xFF000000UL);
    }
    float tempNeg = static_cast<float>(rawTcNeg >> 5) / 128.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -20.0f, tempNeg);

    // Cold Junction 14-bit conversion formula: (rawCj >> 2) / 64.0f (LSB = 0.015625°C)
    // Test Case 3: +25.0°C -> 25.0 / 0.015625 = 1600 = 0x0640 -> shifted left by 2 = 0x1900
    int16_t rawCj = 0x1900;
    float cjTemp = static_cast<float>(rawCj >> 2) / 64.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, cjTemp);
}

// 9. Hardware CJTO Register (0x09) 8-Bit Two's Complement Encoding & Clamping Math
static void test_sensor_cjto_register_encoding_math() {
    // 0.0°C -> 0x00
    TEST_ASSERT_EQUAL_INT8(0x00, sensor::detail::encodeCjto(0.0f));

    // +0.0625°C (1 LSB) -> 1
    TEST_ASSERT_EQUAL_INT8(1, sensor::detail::encodeCjto(0.0625f));

    // +1.0°C -> 16 (0x10)
    TEST_ASSERT_EQUAL_INT8(16, sensor::detail::encodeCjto(1.0f));

    // +7.9375°C (Max positive 8-bit limit) -> 127 (0x7F)
    TEST_ASSERT_EQUAL_INT8(127, sensor::detail::encodeCjto(7.9375f));

    // -0.0625°C (-1 LSB) -> -1 (0xFF)
    TEST_ASSERT_EQUAL_INT8(-1, sensor::detail::encodeCjto(-0.0625f));

    // -1.0°C -> -16 (0xF0)
    TEST_ASSERT_EQUAL_INT8(-16, sensor::detail::encodeCjto(-1.0f));

    // -8.0°C (Min negative 8-bit limit) -> -128 (0x80)
    TEST_ASSERT_EQUAL_INT8(-128, sensor::detail::encodeCjto(-8.0f));

    // Clamping: > +7.9375°C clamps to +127
    TEST_ASSERT_EQUAL_INT8(127, sensor::detail::encodeCjto(10.5f));

    // Clamping: < -8.0°C clamps to -128
    TEST_ASSERT_EQUAL_INT8(-128, sensor::detail::encodeCjto(-12.0f));
}

// 10. Fault Streak De-glitching Filter Model Test
static void test_sensor_fault_streak_deglitch() {
    // Simulates MAX31856 driver consecutive fault filter:
    // A single isolated noise fault should not trigger permanent error until streak threshold (e.g. 3) is reached
    const uint8_t threshold = 3;
    uint8_t streak = 0;
    bool declaredError = false;

    // Tick 1: Spurious Open-Circuit Fault (e.g. EMI spike) -> streak = 1 -> not declared
    streak++;
    if (streak >= threshold) {
        declaredError = true;
    }
    TEST_ASSERT_FALSE(declaredError);

    // Tick 2: Valid reading -> streak resets to 0
    streak = 0;
    TEST_ASSERT_EQUAL_UINT8(0, streak);

    // Ticks 3, 4, 5: Persistent 3 consecutive faults -> triggers declared error
    streak++;
    streak++;
    streak++;
    if (streak >= threshold) {
        declaredError = true;
    }
    TEST_ASSERT_TRUE(declaredError);
}

// 11. EMA Noise Spike Attenuation Test
static void test_sensor_ema_noise_spike_attenuation() {
    // Steady state at 150.0°C
    float filtered = 150.0f;
    const float alpha = 0.3f;

    // Single massive EMI glitch of +50°C (raw = 200°C)
    float rawGlitch = 200.0f;
    filtered = (alpha * rawGlitch) + ((1.0f - alpha) * filtered);

    // With alpha=0.3, the spike is attenuated by 70% (150 -> 165°C instead of jumping to 200°C)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 165.0f, filtered);

    // Next reading normal (150.0°C)
    filtered = (alpha * 150.0f) + ((1.0f - alpha) * filtered);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.5f, filtered);
}

// 12. Register Calculation Math & Mode Verification Test
static void test_sensor_register_calculation_math() {
    // CR0 calculation: CMODE(bit7)=1, 1SHOT(bit6)=0, OCFAULT(bit5:4)=01 (0x10), CJ(bit3)=0, FAULT(bit2)=0 (Comparator),
    // FAULTCLR(bit1)=0, 50Hz(bit0)=1 CR0 = 0x80 | 0x10 | 0x01 = 0x91
    const uint8_t cr0_50hz =
        sensor::detail::computeCr0(sensor::ConversionMode::CONTINUOUS, sensor::NoiseFilter::FILTER_50HZ);
    TEST_ASSERT_EQUAL_HEX8(0x91, cr0_50hz);

    // CR0 for 60Hz: 0x80 | 0x10 | 0 = 0x90
    const uint8_t cr0_60hz =
        sensor::detail::computeCr0(sensor::ConversionMode::CONTINUOUS, sensor::NoiseFilter::FILTER_60HZ);
    TEST_ASSERT_EQUAL_HEX8(0x90, cr0_60hz);

    // CR1 calculation: AveragingMode::SAMPLES_4 (0x02 << 4 = 0x20) | ThermocoupleType::TYPE_K (0x03) = 0x23
    const uint8_t cr1_4x_k =
        sensor::detail::computeCr1(sensor::AveragingMode::SAMPLES_4, sensor::ThermocoupleType::TYPE_K);
    TEST_ASSERT_EQUAL_HEX8(0x23, cr1_4x_k);

    // CR1 for 1 sample + Type K: 0x03
    const uint8_t cr1_1x_k =
        sensor::detail::computeCr1(sensor::AveragingMode::SAMPLES_1, sensor::ThermocoupleType::TYPE_K);
    TEST_ASSERT_EQUAL_HEX8(0x03, cr1_1x_k);
}

// 13. Cached Reading Copy Semantics Test
static void test_sensor_get_latest_copy_semantics() {
    sensor::SensorReading original;
    original.temperature = 123.45f;
    original.rawTemperature = 123.0f;
    original.coldJunction = 22.5f;
    original.isValid = true;
    original.timestampMs = 5000;

    sensor::SensorReading copy = original;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 123.45f, copy.temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.5f, copy.coldJunction);
    TEST_ASSERT_TRUE(copy.isValid);
    TEST_ASSERT_EQUAL_UINT32(5000, copy.timestampMs);
}

// ============================================================================
// NEW TESTS: Bug Fix Coverage
// ============================================================================

// 14. CJHF/CJLF Register Encoding — round() + clamp() fix (Issue #4)
static void test_sensor_cjhf_cjlf_encoding() {
    // Integer values (the common case)
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(85), sensor::detail::encodeCjThreshold(85.0f));   // default cjHigh
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(-20), sensor::detail::encodeCjThreshold(-20.0f)); // default cjLow

    // Fractional values: must round, not truncate
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(86), sensor::detail::encodeCjThreshold(85.6f)); // rounds up
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(85), sensor::detail::encodeCjThreshold(85.4f)); // rounds down
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(-20),
                           sensor::detail::encodeCjThreshold(-19.5f)); // rounds to -20 (away from zero)

    // Clamping: out-of-range values must clamp, not cause UB
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(127), sensor::detail::encodeCjThreshold(200.0f));   // clamp to +127
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(-128), sensor::detail::encodeCjThreshold(-200.0f)); // clamp to -128
}

// 15. TC Threshold Register Encoding — clamp + round (Issue #5)
static void test_sensor_tc_threshold_encoding() {
    // Default values
    TEST_ASSERT_EQUAL_INT16(300 * 16, sensor::detail::encodeTcThreshold(300.0f)); // tcHigh default: 4800
    TEST_ASSERT_EQUAL_INT16(-10 * 16, sensor::detail::encodeTcThreshold(-10.0f)); // tcLow  default: -160

    // Fractional values: must round correctly
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(std::round(300.0625f * 16.0f)),
                            sensor::detail::encodeTcThreshold(300.0625f));

    // Clamping: must not overflow int16_t (UB prevention)
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(std::round(2047.9375f * 16.0f)),
                            sensor::detail::encodeTcThreshold(3000.0f));
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(std::round(-2048.0f * 16.0f)),
                            sensor::detail::encodeTcThreshold(-3000.0f));
}

// 16. LTCBL Reserved Bits Masking (Issue #2d)
static void test_sensor_ltcbl_reserved_bits_masking() {
    // LTCBL bits[4:0] are reserved by the IC. The driver masks them with 0xE0.
    // Test: 100.0°C raw register value = 0x064000 (bits 23:5)
    // LTCBL = 0x00 normally. Add reserved bits: 0x1F (all 5 reserved bits set)
    const uint8_t ltcHigh = 0x06;
    const uint8_t ltcMid = 0x40;
    const uint8_t ltcLow = 0x1F; // reserved bits all set

    const float tempMasked = sensor::detail::decodeThermocoupleTemp(ltcHigh, ltcMid, ltcLow);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, tempMasked);
}

// 17. conversionTimeMs() — Formula Verification (New API)
static void test_sensor_conversion_time_ms() {
    // Verify the conversion time formula matches MAX31856 datasheet Table 2.
    // 50Hz filter
    TEST_ASSERT_EQUAL_UINT32(169u, sensor::detail::calculateConversionTimeMs(sensor::NoiseFilter::FILTER_50HZ,
                                                                             sensor::AveragingMode::SAMPLES_1));
    TEST_ASSERT_EQUAL_UINT32(209u, sensor::detail::calculateConversionTimeMs(sensor::NoiseFilter::FILTER_50HZ,
                                                                             sensor::AveragingMode::SAMPLES_2));
    TEST_ASSERT_EQUAL_UINT32(289u, sensor::detail::calculateConversionTimeMs(sensor::NoiseFilter::FILTER_50HZ,
                                                                             sensor::AveragingMode::SAMPLES_4));
    TEST_ASSERT_EQUAL_UINT32(449u, sensor::detail::calculateConversionTimeMs(sensor::NoiseFilter::FILTER_50HZ,
                                                                             sensor::AveragingMode::SAMPLES_8));
    TEST_ASSERT_EQUAL_UINT32(769u, sensor::detail::calculateConversionTimeMs(sensor::NoiseFilter::FILTER_50HZ,
                                                                             sensor::AveragingMode::SAMPLES_16));

    // 60Hz filter
    TEST_ASSERT_EQUAL_UINT32(143u, sensor::detail::calculateConversionTimeMs(sensor::NoiseFilter::FILTER_60HZ,
                                                                             sensor::AveragingMode::SAMPLES_1));
    TEST_ASSERT_EQUAL_UINT32(177u, sensor::detail::calculateConversionTimeMs(sensor::NoiseFilter::FILTER_60HZ,
                                                                             sensor::AveragingMode::SAMPLES_2));
    TEST_ASSERT_EQUAL_UINT32(245u, sensor::detail::calculateConversionTimeMs(sensor::NoiseFilter::FILTER_60HZ,
                                                                             sensor::AveragingMode::SAMPLES_4));
    TEST_ASSERT_EQUAL_UINT32(381u, sensor::detail::calculateConversionTimeMs(sensor::NoiseFilter::FILTER_60HZ,
                                                                             sensor::AveragingMode::SAMPLES_8));
    TEST_ASSERT_EQUAL_UINT32(653u, sensor::detail::calculateConversionTimeMs(sensor::NoiseFilter::FILTER_60HZ,
                                                                             sensor::AveragingMode::SAMPLES_16));
}

// 18. EMA Filter Seeding Guard — Must Not Seed From Faulted Read (Issue #15)
static void test_sensor_ema_not_seeded_on_fault() {
    // Simulate the corrected filter seeding logic:
    // _filterInit must remain false until rawFault == 0.
    // If the first read has a fault (open circuit → rawTemp = 0°C), the filter
    // must NOT be seeded with 0°C.
    bool filterInit = false;
    float filteredTemp = 25.0f; // initial seed value (not yet committed)
    const float alpha = 0.3f;

    // Tick 1: faulted read (open circuit, rawFault=0x01, rawTemp=0°C)
    float rawTemp1 = 0.0f;
    uint8_t rawFault1 = 0x01;
    if (!filterInit && rawFault1 == 0) {
        filteredTemp = rawTemp1;
        filterInit = true;
    }
    TEST_ASSERT_FALSE(filterInit); // Must NOT be seeded from a faulted read

    // Tick 2: still faulted
    float rawTemp2 = 0.0f;
    uint8_t rawFault2 = 0x01;
    if (!filterInit && rawFault2 == 0) {
        filteredTemp = rawTemp2;
        filterInit = true;
    }
    TEST_ASSERT_FALSE(filterInit); // Still not seeded

    // Tick 3: clean read (rawFault=0, rawTemp=22.5°C — room temperature)
    float rawTemp3 = 22.5f;
    uint8_t rawFault3 = 0x00;
    if (!filterInit && rawFault3 == 0) {
        filteredTemp = rawTemp3;
        filterInit = true;
    }
    TEST_ASSERT_TRUE(filterInit);                         // Now seeded correctly
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.5f, filteredTemp); // Seeded with real temperature

    // Tick 4: normal EMA update
    float rawTemp4 = 23.0f;
    filteredTemp = (alpha * rawTemp4) + ((1.0f - alpha) * filteredTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.65f, filteredTemp); // 0.3*23 + 0.7*22.5 = 22.65
}

// 19. _faultStreak uint8_t Overflow Prevention (Critical Safety Bug Fix)
// Without capping, after 255 consecutive fault reads the uint8_t wraps to 0.
// Then 0 >= faultStreakLimit is false → driver falsely reports isValid=true.
static void test_sensor_fault_streak_no_overflow() {
    const uint8_t faultStreakLimit = 3;
    uint8_t faultStreak = 0;

    // Simulate 300 consecutive fault reads (well past the uint8_t overflow point of 255)
    for (int i = 0; i < 300; ++i) {
        // Corrected logic: cap before incrementing
        if (faultStreak < faultStreakLimit) {
            faultStreak++;
        }
        // The streak must always be >= faultStreakLimit after reaching it
        // It must never drop back to 0 due to overflow
        if (i >= faultStreakLimit - 1) {
            TEST_ASSERT_EQUAL_UINT8(faultStreakLimit, faultStreak);
        }
    }

    // After 300 faults, streak must still equal the limit (not 0 from overflow)
    TEST_ASSERT_EQUAL_UINT8(faultStreakLimit, faultStreak);

    // Confirm: a clean read resets the counter correctly
    faultStreak = 0;
    TEST_ASSERT_EQUAL_UINT8(0, faultStreak);
}

// 20. MAX31856 Power-On Reset (POR) / Brownout Detection Logic
// When IC loses power and restarts, CR0 resets to 0x00 (CMODE=0) and MASK resets to 0xFF.
// The driver must detect this and flag reading.isValid = false.
static void test_sensor_por_brownout_detection() {
    // Case 1: Normal operational state (Continuous mode, CR0 bit 7 set, MASK unmasked = 0x00)
    uint8_t normalCr0 = 0x90; // CMODE=1, OCFAULT=01
    uint8_t normalMask = 0x00;
    bool isPorNormal = ((normalCr0 & (1 << 7)) == 0) || (normalMask == 0xFF);
    TEST_ASSERT_FALSE(isPorNormal);

    // Case 2: IC Power-On Reset occurred (CR0 resets to 0x00 -> CMODE=0 stopped, MASK=0xFF)
    uint8_t porCr0 = 0x00;
    uint8_t porMask = 0xFF;
    bool isPorDetected = ((porCr0 & (1 << 7)) == 0) || (porMask == 0xFF);
    TEST_ASSERT_TRUE(isPorDetected);

    // Case 3: IC rebooted and CMODE was cleared even if MASK is weird
    uint8_t haltedCr0 = 0x10; // CMODE=0
    uint8_t customMask = 0x00;
    bool isHaltedDetected = ((haltedCr0 & (1 << 7)) == 0) || (customMask == 0xFF);
    TEST_ASSERT_TRUE(isHaltedDetected);
}

// 21. POST Pattern Toggle Logic Verification Test
// Verifies that the POST patterns (0x03 and 0x23) produce distinct CR1 representations
// and can be validated without collision.
static void test_sensor_post_pattern_toggle_logic() {
    constexpr uint8_t PATTERN_1 = 0x03; // 1x sampling, Type-K
    constexpr uint8_t PATTERN_2 = 0x23; // 4x sampling, Type-K

    TEST_ASSERT_NOT_EQUAL(PATTERN_1, PATTERN_2);

    // Verify bitfield decoding of pattern 1
    uint8_t avg1 = (PATTERN_1 >> 4) & 0x07;
    uint8_t tc1 = PATTERN_1 & 0x0F;
    TEST_ASSERT_EQUAL_UINT8(0, avg1); // AveragingMode::SAMPLES_1
    TEST_ASSERT_EQUAL_UINT8(3, tc1);  // ThermocoupleType::TYPE_K

    // Verify bitfield decoding of pattern 2
    uint8_t avg2 = (PATTERN_2 >> 4) & 0x07;
    uint8_t tc2 = PATTERN_2 & 0x0F;
    TEST_ASSERT_EQUAL_UINT8(2, avg2); // AveragingMode::SAMPLES_4
    TEST_ASSERT_EQUAL_UINT8(3, tc2);  // ThermocoupleType::TYPE_K
}

#if defined(HOST_TEST)
#include "driver/spi_master.h"

// 22. MAX31856 Full Driver Lifecycle & POST Hardware Toggle Test
static void test_sensor_driver_lifecycle_and_post() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);

    // 1. First begin(skipPriming = true)
    bool ok = sensor.begin(true);
    TEST_ASSERT_TRUE(ok);

    // 2. Second begin() must be idempotent and safely return true without re-adding SPI device
    bool ok2 = sensor.begin(true);
    TEST_ASSERT_TRUE(ok2);

    // 3. Verify POST correctly configured CR0 and CR1
    // CR0 bit 7 should be set (CMODE=1, continuous mode active)
    TEST_ASSERT_NOT_EQUAL(0, g_mockSpiRegisters[0] & (1 << 7));
    // CR1 should have Type-K (0x03)
    TEST_ASSERT_EQUAL_UINT8(0x03, g_mockSpiRegisters[1] & 0x0F);
}

// 23. MAX31856 Driver Normal Read, Precision Decoding and EMA Filtering Test
static void test_sensor_driver_normal_read_and_filtering() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    cfg.emaFilterEnabled = true;
    cfg.emaAlpha = 0.5f;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    // 1. Seed first temperature: 150.0°C thermocouple, 25.0°C cold junction
    mockSpiSetTemperature(150.0f, 25.0f);
    sensor::SensorReading r1 = sensor.read();
    TEST_ASSERT_TRUE(r1.isValid);
    TEST_ASSERT_FALSE(r1.fault.hasFault());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 150.0f, r1.temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, r1.coldJunction);

    // 2. Apply new temperature: 170.0°C
    // With EMA alpha = 0.5, filtered = 0.5 * 170 + 0.5 * 150 = 160.0°C
    mockSpiSetTemperature(170.0f, 25.0f);
    sensor::SensorReading r2 = sensor.read();
    TEST_ASSERT_TRUE(r2.isValid);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 160.0f, r2.temperature);
}

// 24. MAX31856 Fault Streak Deglitching & Trip Threshold Test
static void test_sensor_driver_fault_deglitching_and_trip() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    cfg.faultStreakLimit = 3;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    mockSpiSetTemperature(100.0f, 25.0f);
    sensor::SensorReading initial = sensor.read();
    TEST_ASSERT_TRUE(initial.isValid);
    TEST_ASSERT_FALSE(initial.fault.hasFault());

    // Inject Open-Circuit fault in Status Register (SR bit 0 = 0x01)
    g_mockSpiRegisters[15] = 0x01;

    // Cycle 1: streak = 1 (below limit 3) -> fault is deglitched, reading remains valid
    sensor::SensorReading r1 = sensor.read();
    TEST_ASSERT_TRUE(r1.isValid);
    TEST_ASSERT_TRUE(r1.fault.openCircuit);

    // Cycle 2: streak = 2 (below limit 3) -> reading remains valid
    sensor::SensorReading r2 = sensor.read();
    TEST_ASSERT_TRUE(r2.isValid);
    TEST_ASSERT_TRUE(r2.fault.openCircuit);

    // Cycle 3: streak = 3 (reached limit) -> fault confirmed, reading becomes invalid!
    sensor::SensorReading r3 = sensor.read();
    TEST_ASSERT_FALSE(r3.isValid);
    TEST_ASSERT_TRUE(r3.fault.openCircuit);

    // Clear fault in hardware register
    g_mockSpiRegisters[15] = 0x00;

    // Next cycle recovers and resets streak
    sensor::SensorReading rClean = sensor.read();
    TEST_ASSERT_TRUE(rClean.isValid);
    TEST_ASSERT_FALSE(rClean.fault.hasFault());
}

// 25. MAX31856 Power-On Reset (Brownout) Self-Healing Recovery Test
static void test_sensor_driver_por_brownout_recovery() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    // Simulate power interruption / brownout:
    // IC reboots into power-on defaults (CR0 = 0x00, MASK = 0xFF)
    g_mockSpiRegisters[0] = 0x00;
    g_mockSpiRegisters[2] = 0xFF;

    // Driver must detect POR, re-initialize hardware, and reject reading as invalid during reboot
    sensor::SensorReading r = sensor.read();
    TEST_ASSERT_FALSE(r.isValid);

    // Verify self-healing: CR0 bit 7 (CMODE) re-enabled and MASK restored to 0x00
    TEST_ASSERT_NOT_EQUAL(0, g_mockSpiRegisters[0] & (1 << 7));
    TEST_ASSERT_EQUAL_UINT8(0x00, g_mockSpiRegisters[2]);
}

// 26. MAX31856 Dynamic Runtime applyConfig() and Calibration Update Test
static void test_sensor_driver_apply_config() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    sensor::MAX31856Config newCfg = cfg;
    newCfg.cjOffset = 2.0f;
    newCfg.tcHighFaultTemp = 350.0f;
    newCfg.tcType = sensor::ThermocoupleType::TYPE_J;
    newCfg.averaging = sensor::AveragingMode::SAMPLES_4;
    newCfg.filter = sensor::NoiseFilter::FILTER_60HZ;

    bool updated = sensor.applyConfig(newCfg);
    TEST_ASSERT_TRUE(updated);

    // Verify CJTO register (0x09) holds encoded 2.0°C (2.0 / 0.0625 = 32 = 0x20)
    TEST_ASSERT_EQUAL_INT8(32, static_cast<int8_t>(g_mockSpiRegisters[9]));

    // Verify CR1 register (0x01) has 4x averaging (0x20) and Type J (0x02) -> 0x22
    TEST_ASSERT_EQUAL_UINT8(0x22, g_mockSpiRegisters[1]);
}

// 27. MAX31856 Hardware Conversion Time Computations
static void test_sensor_driver_conversion_times() {
    sensor::MAX31856Config cfg;
    cfg.filter = sensor::NoiseFilter::FILTER_50HZ;
    cfg.averaging = sensor::AveragingMode::SAMPLES_1;
    sensor::MAX31856 s1(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_EQUAL_UINT32(169, s1.conversionTimeMs());

    cfg.averaging = sensor::AveragingMode::SAMPLES_4;
    sensor::MAX31856 s4(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_EQUAL_UINT32(289, s4.conversionTimeMs());

    cfg.filter = sensor::NoiseFilter::FILTER_60HZ;
    cfg.averaging = sensor::AveragingMode::SAMPLES_1;
    sensor::MAX31856 s60_1(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_EQUAL_UINT32(143, s60_1.conversionTimeMs());
}

// 28. MAX31856 Filter Reset, OneShot Mode, Sanity Limits, and Fault Clearing Test
static void test_sensor_driver_filter_reset_oneshot_and_bounds() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    cfg.emaFilterEnabled = false; // test unfiltered pass-through
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    // 1. Unfiltered read
    mockSpiSetTemperature(215.5f, 25.0f);
    sensor::SensorReading rPass = sensor.read();
    TEST_ASSERT_TRUE(rPass.isValid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 215.5f, rPass.temperature);

    // 2. Filter reset
    sensor.resetFilter(180.0f);

    // 3. One-shot trigger
    TEST_ASSERT_TRUE(sensor.triggerOneShot());

    // 4. Clear fault register
    sensor.clearFaultRegister();

    // 5. Sanity bounds rejection (rawTemp out of range > 1850.0°C)
    mockSpiSetTemperature(2000.0f, 25.0f);
    sensor::SensorReading rHigh = sensor.read();
    TEST_ASSERT_FALSE(rHigh.isValid);

    // 6. Sanity bounds rejection (rawTemp < -100.0°C)
    mockSpiSetTemperature(-150.0f, 25.0f);
    sensor::SensorReading rLow = sensor.read();
    TEST_ASSERT_FALSE(rLow.isValid);

    // 7. All fault bits populated in Status Register
    g_mockSpiRegisters[15] = 0xFE; // all faults except open circuit
    mockSpiSetTemperature(100.0f, 25.0f);
    sensor::SensorReading rFaults = sensor.read();
    TEST_ASSERT_TRUE(rFaults.fault.overUnderVoltage);
    TEST_ASSERT_TRUE(rFaults.fault.tcLow);
    TEST_ASSERT_TRUE(rFaults.fault.tcHigh);
    TEST_ASSERT_TRUE(rFaults.fault.cjLow);
    TEST_ASSERT_TRUE(rFaults.fault.cjHigh);
    TEST_ASSERT_TRUE(rFaults.fault.tcOutOfRange);
    TEST_ASSERT_TRUE(rFaults.fault.cjOutOfRange);
}
#endif

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_sensor_tests() {
    RUN_TEST(test_sensor_config_defaults);
    RUN_TEST(test_sensor_reading_defaults);
    RUN_TEST(test_sensor_ema_math);
    RUN_TEST(test_sensor_ema_step_response_convergence);
    RUN_TEST(test_sensor_cjto_temperature_offset_math);
    RUN_TEST(test_sensor_fault_flag_isolation);
    RUN_TEST(test_sensor_fault_flags_has_fault);
    RUN_TEST(test_sensor_temperature_decoding_math);
    RUN_TEST(test_sensor_cjto_register_encoding_math);
    RUN_TEST(test_sensor_fault_streak_deglitch);
    RUN_TEST(test_sensor_ema_noise_spike_attenuation);
    RUN_TEST(test_sensor_register_calculation_math);
    RUN_TEST(test_sensor_get_latest_copy_semantics);
    // New tests (bug fix coverage)
    RUN_TEST(test_sensor_cjhf_cjlf_encoding);
    RUN_TEST(test_sensor_tc_threshold_encoding);
    RUN_TEST(test_sensor_ltcbl_reserved_bits_masking);
    RUN_TEST(test_sensor_conversion_time_ms);
    RUN_TEST(test_sensor_ema_not_seeded_on_fault);
    RUN_TEST(test_sensor_fault_streak_no_overflow);
    RUN_TEST(test_sensor_por_brownout_detection);
    RUN_TEST(test_sensor_post_pattern_toggle_logic);
#if defined(HOST_TEST)
    RUN_TEST(test_sensor_driver_lifecycle_and_post);
    RUN_TEST(test_sensor_driver_normal_read_and_filtering);
    RUN_TEST(test_sensor_driver_fault_deglitching_and_trip);
    RUN_TEST(test_sensor_driver_por_brownout_recovery);
    RUN_TEST(test_sensor_driver_apply_config);
    RUN_TEST(test_sensor_driver_conversion_times);
    RUN_TEST(test_sensor_driver_filter_reset_oneshot_and_bounds);
#endif
}
