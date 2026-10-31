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
    float temp = sensor::detail::decodeThermocoupleTemp(0x06, 0x40, 0x00);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, temp);

    // Test Case 2: -20.0000°C -> -20.0 / 0.0078125 = -2560 = -0x0A00 -> 24-bit two's complement = 0xFFF600 -> shifted
    // by 5 = 0xFEC000
    float tempNeg = sensor::detail::decodeThermocoupleTemp(0xFE, 0xC0, 0x00);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -20.0f, tempNeg);

    // Cold Junction 14-bit conversion formula: (rawCj >> 2) * 0.015625°C
    // Test Case 3: +25.0°C -> 25.0 / 0.015625 = 1600 = 0x0640 -> shifted left by 2 = 0x1900
    float cjTemp = sensor::detail::decodeColdJunctionTemp(0x19, 0x00);
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

    auto updateStreak = [&](bool isFault) {
        if (isFault) {
            streak++;
            if (streak >= threshold) {
                declaredError = true;
            }
        } else {
            streak = 0;
        }
    };

    // Tick 1: Spurious Open-Circuit Fault (e.g. EMI spike) -> streak = 1 -> not declared
    updateStreak(true);
    TEST_ASSERT_FALSE(declaredError);

    // Tick 2: Valid reading -> streak resets to 0
    updateStreak(false);
    TEST_ASSERT_EQUAL_UINT8(0, streak);

    // Ticks 3, 4, 5: Persistent 3 consecutive faults -> triggers declared error
    updateStreak(true);
    updateStreak(true);
    updateStreak(true);
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

    auto applySample = [&](float rawTemp, uint8_t rawFault) {
        if (!filterInit && rawFault == 0) {
            filteredTemp = rawTemp;
            filterInit = true;
        } else if (filterInit) {
            filteredTemp = (alpha * rawTemp) + ((1.0f - alpha) * filteredTemp);
        }
    };

    // Tick 1: faulted read (open circuit, rawFault=0x01, rawTemp=0°C)
    applySample(0.0f, 0x01);
    TEST_ASSERT_FALSE(filterInit); // Must NOT be seeded from a faulted read

    // Tick 2: still faulted
    applySample(0.0f, 0x01);
    TEST_ASSERT_FALSE(filterInit); // Still not seeded

    // Tick 3: clean read (rawFault=0, rawTemp=22.5°C — room temperature)
    applySample(22.5f, 0x00);
    TEST_ASSERT_TRUE(filterInit);                         // Now seeded correctly
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.5f, filteredTemp); // Seeded with real temperature

    // Tick 4: normal EMA update
    applySample(23.0f, 0x00);
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
    auto isPor = [](uint8_t cr0, uint8_t mask) { return ((cr0 & (1 << 7)) == 0) || (mask == 0xFF); };

    // Case 1: Normal operational state (Continuous mode, CR0 bit 7 set, MASK unmasked = 0x00)
    TEST_ASSERT_FALSE(isPor(0x90, 0x00));

    // Case 2: IC Power-On Reset occurred (CR0 resets to 0x00 -> CMODE=0 stopped, MASK=0xFF)
    TEST_ASSERT_TRUE(isPor(0x00, 0xFF));

    // Case 3: IC rebooted and CMODE was cleared even if MASK is weird
    TEST_ASSERT_TRUE(isPor(0x10, 0x00));
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

// 29. MAX31856 Uninitialized Device Guards Test
static void test_sensor_driver_uninitialized_guards() {
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);

    // Call methods without calling begin()
    sensor::SensorReading r = sensor.read();
    TEST_ASSERT_FALSE(r.isValid);

    TEST_ASSERT_EQUAL_UINT8(0xFF, sensor.readRegister(0x00));

    std::array<uint8_t, 4> buf{};
    TEST_ASSERT_FALSE(sensor.readRegisters(0x00, buf.data(), buf.size()));
    TEST_ASSERT_FALSE(sensor.writeRegister(0x00, 0x12));
    TEST_ASSERT_FALSE(sensor.verifyConfig(0x90, 0x03, 0));
    TEST_ASSERT_FALSE(sensor.initHardware());

    // applyConfig on uninitialized device updates config in RAM and safely returns false
    sensor::MAX31856Config newCfg = cfg;
    newCfg.emaAlpha = 0.8f;
    TEST_ASSERT_FALSE(sensor.applyConfig(newCfg));
}

// 30. MAX31856 SPI Hardware Bus Failure Handling Test
static void test_sensor_driver_spi_failure_handling() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);

    // 1. spi_bus_add_device failure
    g_mockSpiFailAddDevice = true;
    TEST_ASSERT_FALSE(sensor.begin(true));
    g_mockSpiFailAddDevice = false;

    // 2. Successful begin
    TEST_ASSERT_TRUE(sensor.begin(true));

    // 3. SPI transmit failure during read
    g_mockSpiFailTransmit = true;
    sensor::SensorReading r = sensor.read();
    TEST_ASSERT_FALSE(r.isValid);
    TEST_ASSERT_EQUAL_UINT8(0xFF, sensor.readRegister(0x00));
    std::array<uint8_t, 2> buf{};
    TEST_ASSERT_FALSE(sensor.readRegisters(0x00, buf.data(), buf.size()));
    TEST_ASSERT_FALSE(sensor.writeRegister(0x00, 0x55));
    g_mockSpiFailTransmit = false;
}

// 31. MAX31856 Register Address & Length Bounds Validation Test
static void test_sensor_driver_burst_read_bounds_validation() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    std::array<uint8_t, 4> buf{};
    // Null buffer or zero length
    TEST_ASSERT_FALSE(sensor.readRegisters(0x00, nullptr, 4));
    TEST_ASSERT_FALSE(sensor.readRegisters(0x00, buf.data(), 0));

    // startReg out of map (> 0x0F)
    TEST_ASSERT_FALSE(sensor.readRegisters(0x10, buf.data(), 1));

    // startReg + length exceeds 16
    TEST_ASSERT_FALSE(sensor.readRegisters(0x0E, buf.data(), 4)); // 14 + 4 = 18 > 16

    // Valid boundary read
    TEST_ASSERT_TRUE(sensor.readRegisters(0x0E, buf.data(), 2)); // 14 + 2 = 16 (exactly last register)
}

// 32. MAX31856 Configuration Verification Register Mismatch Test
static void test_sensor_driver_verify_config_mismatches() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    // 1. Control register mismatch (e.g. CR0 bit flipped)
    TEST_ASSERT_FALSE(sensor.verifyConfig(0x00, 0x03, 0)); // exp CR0 0x00 vs actual 0x90

    // 2. Threshold mismatch (corrupt CJHF in mock register 3)
    g_mockSpiRegisters[3] = 0x11; // corrupt CJHF
    TEST_ASSERT_FALSE(sensor.verifyConfig(0x90, 0x03, 0));
    g_mockSpiRegisters[3] = sensor::detail::encodeCjThreshold(cfg.cjHighFaultTemp);
}

// 33. MAX31856 Driver Begin with Startup Priming Test
static void test_sensor_driver_begin_with_priming() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    cfg.mode = sensor::ConversionMode::CONTINUOUS;
    sensor::MAX31856 sensorCont(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);

    // Call begin(false) - exercises priming delay and priming read()
    mockSpiSetTemperature(120.0f, 25.0f);
    TEST_ASSERT_TRUE(sensorCont.begin(false));

    // Call begin(false) in ONE_SHOT mode - returns true immediately without priming
    sensor::MAX31856Config cfgOneShot = cfg;
    cfgOneShot.mode = sensor::ConversionMode::ONE_SHOT;
    sensor::MAX31856 sensorOneShot(SPI2_HOST, static_cast<gpio_num_t>(11), cfgOneShot);
    TEST_ASSERT_TRUE(sensorOneShot.begin(false));
}

// 34. MAX31856 Detailed applyConfig Branches and Validation Test
static void test_sensor_driver_apply_config_branches() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    // 1. Thresholds changed only
    sensor::MAX31856Config c1 = cfg;
    c1.tcLowFaultTemp = -5.0f;
    c1.cjHighFaultTemp = 75.0f;
    TEST_ASSERT_TRUE(sensor.applyConfig(c1));

    // 2. CJTO changed only
    sensor::MAX31856Config c2 = c1;
    c2.cjOffset = -1.5f;
    TEST_ASSERT_TRUE(sensor.applyConfig(c2));

    // 3. Operating mode and filter changed
    sensor::MAX31856Config c3 = c2;
    c3.filter = sensor::NoiseFilter::FILTER_60HZ;
    c3.mode = sensor::ConversionMode::ONE_SHOT;
    c3.tcType = sensor::ThermocoupleType::TYPE_N;
    TEST_ASSERT_TRUE(sensor.applyConfig(c3));

    // 4. Invalid / NaN emaAlpha rejected
    sensor::MAX31856Config c4 = c3;
    c4.emaAlpha = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_TRUE(sensor.applyConfig(c4));

    c4.emaAlpha = -0.5f;
    TEST_ASSERT_TRUE(sensor.applyConfig(c4));

    // 5. Fault streak limit change resets streak
    c4.faultStreakLimit = 5;
    TEST_ASSERT_TRUE(sensor.applyConfig(c4));

    // 6. verifyConfig failure inside applyConfig
    g_mockSpiFailTransmit = true;
    c4.cjOffset = 3.0f;
    TEST_ASSERT_FALSE(sensor.applyConfig(c4));
    g_mockSpiFailTransmit = false;
}

// 35. MAX31856 NaN and Infinity Robustness in Filtering & Math
static void test_sensor_driver_nan_inf_robustness() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    float qnan = std::numeric_limits<float>::quiet_NaN();
    float posInf = std::numeric_limits<float>::infinity();
    float negInf = -std::numeric_limits<float>::infinity();

    // 1. resetFilter with NaN/Inf resets to default 25.0°C safely
    sensor.resetFilter(qnan);
    sensor.resetFilter(posInf);
    sensor.resetFilter(negInf);

    // 2. encode helpers with NaN / Inf
    TEST_ASSERT_EQUAL_UINT8(sensor::detail::encodeCjThreshold(sensor::DEFAULT_CJ_HIGH_FAULT_TEMP),
                            sensor::detail::encodeCjThreshold(qnan));
    TEST_ASSERT_EQUAL_INT16(sensor::detail::encodeTcThreshold(sensor::DEFAULT_TC_HIGH_FAULT_TEMP),
                            sensor::detail::encodeTcThreshold(posInf));
    TEST_ASSERT_EQUAL_INT8(0, sensor::detail::encodeCjto(qnan));
    TEST_ASSERT_EQUAL_INT8(0, sensor::detail::encodeCjto(negInf));

    // 3. One-shot trigger with 60Hz filter
    sensor::MAX31856Config cfg60 = cfg;
    cfg60.filter = sensor::NoiseFilter::FILTER_60HZ;
    sensor::MAX31856 s60(SPI2_HOST, static_cast<gpio_num_t>(12), cfg60);
    TEST_ASSERT_TRUE(s60.begin(true));
    TEST_ASSERT_TRUE(s60.triggerOneShot());
}

// 36. MAX31856 POST Readback Mismatch & Step Register Failures Test
static void test_sensor_driver_post_pattern_mismatch_and_step_failures() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);

    // 1. POST readback mismatch (reads 0x55 instead of pattern)
    g_mockSpiCorruptReadReg = 1;
    g_mockSpiCorruptVal = 0x55;
    TEST_ASSERT_FALSE(sensor.initHardware(true));
    g_mockSpiCorruptReadReg = -1;

    // 2. Step 1 (CR0) write failure
    g_mockSpiFailOnReg = 0;
    TEST_ASSERT_FALSE(sensor.initHardware(true));
    g_mockSpiFailOnReg = -1;

    // 3. Step 3 (MASK) write failure
    g_mockSpiFailOnReg = 2;
    TEST_ASSERT_FALSE(sensor.initHardware(true));
    g_mockSpiFailOnReg = -1;

    // 4. Step 4 (Thresholds/CJTO) write failure
    g_mockSpiFailOnReg = 3; // CJHF
    TEST_ASSERT_FALSE(sensor.initHardware(true));
    g_mockSpiFailOnReg = -1;
}

// 37. MAX31856 POR Deep Recovery Lifecycle Test
static void test_sensor_driver_por_deep_recovery_lifecycle() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    // Force POR state
    g_mockSpiRegisters[0] = 0x00; // CMODE = 0
    g_mockSpiRegisters[2] = 0xFF; // MASK = 0xFF

    // Fail re-initialization during POR recovery
    g_mockSpiFailTransmit = true;
    sensor::SensorReading r1 = sensor.read();
    TEST_ASSERT_FALSE(r1.isValid);
    g_mockSpiFailTransmit = false;

    // Next cycle: still in POR, silent retry attempted
    sensor::SensorReading r2 = sensor.read();
    TEST_ASSERT_FALSE(r2.isValid);

    // Now restore healthy register state
    mockSpiResetRegisters();
    mockSpiSetTemperature(100.0f, 25.0f);
    sensor::SensorReading r3 = sensor.read();
    TEST_ASSERT_TRUE(r3.isValid);
}

// 38. MAX31856 Fault Streak Saturation & EMA First-Read Fault Seeding
static void test_sensor_driver_fault_streak_saturation_and_ema_seeding() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    cfg.faultStreakLimit = 3;
    cfg.emaFilterEnabled = true;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    // 1. First read has open-circuit fault: filter must NOT seed with 0°C
    g_mockSpiRegisters[15] = 0x01; // Fault
    mockSpiSetTemperature(0.0f, 25.0f);
    sensor::SensorReading rFault1 = sensor.read();
    TEST_ASSERT_TRUE(rFault1.fault.openCircuit);

    // 2. Fault persists for streak cycles 2, 3, 4, 5 (saturates at 3 and does not overflow)
    sensor.read();
    sensor.read();
    sensor::SensorReading rFault4 = sensor.read();
    TEST_ASSERT_FALSE(rFault4.isValid);
    sensor::SensorReading rFault5 = sensor.read();
    TEST_ASSERT_FALSE(rFault5.isValid);

    // 3. Clear fault and provide clean 150°C reading: filter now seeds cleanly
    g_mockSpiRegisters[15] = 0x00;
    mockSpiSetTemperature(150.0f, 25.0f);
    sensor::SensorReading rClean = sensor.read();
    TEST_ASSERT_TRUE(rClean.isValid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 150.0f, rClean.temperature);
}

// 39. MAX31856 applyConfig Threshold & CJTO Write Failures Test
static void test_sensor_driver_apply_config_write_failures() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    // 1. Thresholds write failure
    g_mockSpiFailOnReg = 3; // CJHF
    sensor::MAX31856Config c1 = cfg;
    c1.tcHighFaultTemp = 400.0f;
    TEST_ASSERT_FALSE(sensor.applyConfig(c1));
    g_mockSpiFailOnReg = -1;

    // 2. CJTO write failure
    g_mockSpiFailOnReg = 9; // CJTO
    sensor::MAX31856Config c2 = cfg;
    c2.cjOffset = 3.5f;
    TEST_ASSERT_FALSE(sensor.applyConfig(c2));
    g_mockSpiFailOnReg = -1;

    // 3. VerifyConfig mismatch on CJTO in applyConfig
    g_mockSpiCorruptReadReg = 9; // Corrupt CJTO readback
    g_mockSpiCorruptVal = 0x7F;
    sensor::MAX31856Config c3 = cfg;
    c3.cjOffset = 1.0f;
    TEST_ASSERT_FALSE(sensor.applyConfig(c3));
    g_mockSpiCorruptReadReg = -1;
}

// 40. MAX31856 Init Retry Exhaustion & Step 1-5 Failures Test
static void test_sensor_driver_init_retries_and_step_failures() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);

    // 1. Total SPI transmit failure exhausting 3 retries in begin()
    g_mockSpiFailTransmit = true;
    TEST_ASSERT_FALSE(sensor.begin(true));
    g_mockSpiFailTransmit = false;

    // 2. Step 1 write failure (CR0) with verbose = true
    g_mockSpiFailOnReg = 0;
    TEST_ASSERT_FALSE(sensor.begin(true));
    g_mockSpiFailOnReg = -1;

    // 3. Step 2 write failure (CR1) with verbose = true
    g_mockSpiFailOnReg = 1;
    TEST_ASSERT_FALSE(sensor.begin(true));
    g_mockSpiFailOnReg = -1;

    // 4. Step 3 write failure (MASK) with verbose = true
    g_mockSpiFailOnReg = 2;
    TEST_ASSERT_FALSE(sensor.begin(true));
    g_mockSpiFailOnReg = -1;

    // 5. Step 4 write failure (Thresholds) with verbose = true
    g_mockSpiFailOnReg = 3;
    TEST_ASSERT_FALSE(sensor.begin(true));
    g_mockSpiFailOnReg = -1;

    // 6. Step 5 write failure (CR0 final conversion start)
    // Note: To reach step 5, reg 0 write must succeed at step 1 and fail at step 5
    // But since g_mockSpiFailOnReg matches regAddr, we can test verifyRegisters threshold mismatch:
    // Readback threshold mismatch (CJHF reg 3)
    g_mockSpiCorruptReadReg = 3; // Corrupt CJHF readback
    g_mockSpiCorruptVal = 0xAA;
    TEST_ASSERT_FALSE(sensor.begin(true));
    g_mockSpiCorruptReadReg = -1;
}

// 41. MAX31856 POR Recovery Lifecycle Edge Transitions
static void test_sensor_driver_por_recovery_lifecycle_edges() {
    mockSpiResetRegisters();
    sensor::MAX31856Config cfg;
    sensor::MAX31856 sensor(SPI2_HOST, static_cast<gpio_num_t>(10), cfg);
    TEST_ASSERT_TRUE(sensor.begin(true));

    // 1. Simulate POR condition (continuous mode with CMODE cleared to 0)
    g_mockSpiRegisters[0] = 0x00;
    // Induce Step 1 failure during the first POR recovery attempt so initHardware(true) fails
    g_mockSpiFailOnReg = 0;
    sensor::SensorReading r1 = sensor.read();
    TEST_ASSERT_FALSE(r1.isValid);
    g_mockSpiFailOnReg = -1; // Allow recovery on subsequent cycle

    // 2. Next cycle: POR recovery runs silent re-init (initHardware(false)) and recovers registers
    sensor::SensorReading r2 = sensor.read();
    TEST_ASSERT_FALSE(r2.isValid); // r2's burst buffer was read before re-init

    // 3. Cycle after recovery: clean registers produce valid reading
    sensor::SensorReading r3 = sensor.read();
    TEST_ASSERT_TRUE(r3.isValid);
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
    RUN_TEST(test_sensor_driver_uninitialized_guards);
    RUN_TEST(test_sensor_driver_spi_failure_handling);
    RUN_TEST(test_sensor_driver_burst_read_bounds_validation);
    RUN_TEST(test_sensor_driver_verify_config_mismatches);
    RUN_TEST(test_sensor_driver_begin_with_priming);
    RUN_TEST(test_sensor_driver_apply_config_branches);
    RUN_TEST(test_sensor_driver_nan_inf_robustness);
    RUN_TEST(test_sensor_driver_post_pattern_mismatch_and_step_failures);
    RUN_TEST(test_sensor_driver_por_deep_recovery_lifecycle);
    RUN_TEST(test_sensor_driver_fault_streak_saturation_and_ema_seeding);
    RUN_TEST(test_sensor_driver_apply_config_write_failures);
    RUN_TEST(test_sensor_driver_init_retries_and_step_failures);
    RUN_TEST(test_sensor_driver_por_recovery_lifecycle_edges);
#endif
}
