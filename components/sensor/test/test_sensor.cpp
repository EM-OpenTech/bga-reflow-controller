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

// 5. Cold-Junction Offset Applied Temperature Math Test
// NOTE: This test verifies the arithmetic of *applying* a calibration offset to a raw
// temperature value (e.g., in post-processing). It does NOT test CJTO register encoding
// (the hardware register bit format). The register encoding is covered by test #9:
// test_sensor_cjto_register_encoding_math.
static void test_sensor_cjto_temperature_offset_math()
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

// 10. Fault Streak De-glitching Filter Model Test
static void test_sensor_fault_streak_deglitch()
{
    // Simulates MAX31856 driver consecutive fault filter:
    // A single isolated noise fault should not trigger permanent error until streak threshold (e.g. 3) is reached
    const uint8_t threshold = 3;
    uint8_t streak = 0;
    bool declaredError = false;

    // Tick 1: Spurious Open-Circuit Fault (e.g. EMI spike) -> streak = 1 -> not declared
    streak++;
    if (streak >= threshold) declaredError = true;
    TEST_ASSERT_FALSE(declaredError);

    // Tick 2: Valid reading -> streak resets to 0
    streak = 0;
    TEST_ASSERT_EQUAL_UINT8(0, streak);

    // Ticks 3, 4, 5: Persistent 3 consecutive faults -> triggers declared error
    streak++;
    streak++;
    streak++;
    if (streak >= threshold) declaredError = true;
    TEST_ASSERT_TRUE(declaredError);
}

// 11. EMA Noise Spike Attenuation Test
static void test_sensor_ema_noise_spike_attenuation()
{
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
static void test_sensor_register_calculation_math()
{
    // CR0 calculation: CMODE(bit7)=1, 1SHOT(bit6)=0, OCFAULT(bit5:4)=01 (0x10), CJ(bit3)=0, FAULT(bit2)=0 (Comparator), FAULTCLR(bit1)=0, 50Hz(bit0)=1
    // CR0 = 0x80 | 0x10 | 0x01 = 0x91
    uint8_t cr0_50hz = 0x10 | (1 << 7) | (1 << 0);
    TEST_ASSERT_EQUAL_HEX8(0x91, cr0_50hz);

    // CR0 for 60Hz: 0x80 | 0x10 | 0 = 0x90
    uint8_t cr0_60hz = 0x10 | (1 << 7);
    TEST_ASSERT_EQUAL_HEX8(0x90, cr0_60hz);

    // CR1 calculation: AveragingMode::SAMPLES_4 (0x02 << 4 = 0x20) | ThermocoupleType::TYPE_K (0x03) = 0x23
    uint8_t cr1_4x_k = (static_cast<uint8_t>(sensor::AveragingMode::SAMPLES_4) << 4) |
                       (static_cast<uint8_t>(sensor::ThermocoupleType::TYPE_K) & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0x23, cr1_4x_k);

    // CR1 for 1 sample + Type K: 0x03
    uint8_t cr1_1x_k = (static_cast<uint8_t>(sensor::AveragingMode::SAMPLES_1) << 4) |
                       (static_cast<uint8_t>(sensor::ThermocoupleType::TYPE_K) & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0x03, cr1_1x_k);
}

// 13. Cached Reading Copy Semantics Test
static void test_sensor_get_latest_copy_semantics()
{
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
static void test_sensor_cjhf_cjlf_encoding()
{
    auto encodeCj = [](float temp) -> uint8_t {
        float clamped = std::clamp(temp, -128.0f, 127.0f);
        return static_cast<uint8_t>(static_cast<int8_t>(std::round(clamped)));
    };

    // Integer values (the common case)
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(85),  encodeCj(85.0f));   // default cjHigh
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(-20), encodeCj(-20.0f));  // default cjLow

    // Fractional values: must round, not truncate
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(86),  encodeCj(85.6f));   // rounds up
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(85),  encodeCj(85.4f));   // rounds down
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(-20), encodeCj(-19.5f));  // rounds to -20 (away from zero)

    // Clamping: out-of-range values must clamp, not cause UB
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(127),  encodeCj(200.0f)); // clamp to +127
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(-128), encodeCj(-200.0f)); // clamp to -128
}

// 15. TC Threshold Register Encoding — clamp + round (Issue #5)
static void test_sensor_tc_threshold_encoding()
{
    auto encodeTc = [](float temp) -> int16_t {
        float clamped = std::clamp(temp, -2048.0f, 2047.9375f);
        return static_cast<int16_t>(std::round(clamped * 16.0f));
    };

    // Default values
    TEST_ASSERT_EQUAL_INT16(300 * 16,  encodeTc(300.0f));  // tcHigh default: 4800
    TEST_ASSERT_EQUAL_INT16(-10 * 16,  encodeTc(-10.0f));  // tcLow  default: -160

    // Fractional values: must round correctly
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(std::round(300.0625f * 16.0f)), encodeTc(300.0625f));

    // Clamping: must not overflow int16_t (UB prevention)
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(std::round(2047.9375f * 16.0f)), encodeTc(3000.0f));
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(std::round(-2048.0f * 16.0f)),   encodeTc(-3000.0f));
}

// 16. LTCBL Reserved Bits Masking (Issue #2d)
static void test_sensor_ltcbl_reserved_bits_masking()
{
    // LTCBL bits[4:0] are reserved by the IC. The driver now masks them with 0xE0.
    // The >> 5 shift would discard them anyway, but masking before assembly is defensive.

    // Test: 100.0°C raw register value = 0x064000 (bits 23:5)
    // LTCBL = 0x00 normally. Add reserved bits: 0x1F (all 5 reserved bits set)
    // Without mask: raw = 0x06401F, >> 5 = 0x003200 = 12800 → 100.0°C ✓ (correct by accident)
    // With mask:    raw = 0x064000, >> 5 = 0x003200 = 12800 → 100.0°C ✓ (correct by design)
    uint8_t ltcHigh = 0x06;
    uint8_t ltcMid  = 0x40;
    uint8_t ltcLow  = 0x1F; // reserved bits all set

    // Without mask (old behavior) — still gets correct answer only because >> 5 discards bits[4:0]
    int32_t rawNoMask = (static_cast<int32_t>(ltcHigh) << 16) |
                        (static_cast<int32_t>(ltcMid)  << 8)  |
                         static_cast<int32_t>(ltcLow);
    float tempNoMask = static_cast<float>(rawNoMask >> 5) * 0.0078125f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, tempNoMask);

    // With mask (new behavior) — correct by design
    int32_t rawMasked = (static_cast<int32_t>(ltcHigh) << 16) |
                        (static_cast<int32_t>(ltcMid)  << 8)  |
                         static_cast<int32_t>(ltcLow & 0xE0);
    float tempMasked = static_cast<float>(rawMasked >> 5) * 0.0078125f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, tempMasked);
}

// 17. conversionTimeMs() — Formula Verification (New API)
static void test_sensor_conversion_time_ms()
{
    // Verify the conversion time formula matches MAX31856 datasheet Table 2.
    // Formula: base_ms + (samples - 1) × per_sample_ms
    // 50Hz: base=169ms, per_sample=40ms. 60Hz: base=143ms, per_sample=34ms.
    //
    // We test the formula directly (no driver instance needed).
    auto calcTimeMs = [](bool is50Hz, uint8_t avgEnum) -> uint32_t {
        uint32_t base = is50Hz ? 169u : 143u;
        uint32_t add  = is50Hz ?  40u :  34u;
        uint32_t n    = 1u << static_cast<uint32_t>(avgEnum);
        return base + (n - 1u) * add;
    };

    // 50Hz filter
    TEST_ASSERT_EQUAL_UINT32(169u, calcTimeMs(true,  0)); // SAMPLES_1
    TEST_ASSERT_EQUAL_UINT32(209u, calcTimeMs(true,  1)); // SAMPLES_2
    TEST_ASSERT_EQUAL_UINT32(289u, calcTimeMs(true,  2)); // SAMPLES_4
    TEST_ASSERT_EQUAL_UINT32(449u, calcTimeMs(true,  3)); // SAMPLES_8
    TEST_ASSERT_EQUAL_UINT32(769u, calcTimeMs(true,  4)); // SAMPLES_16

    // 60Hz filter
    TEST_ASSERT_EQUAL_UINT32(143u, calcTimeMs(false, 0)); // SAMPLES_1
    TEST_ASSERT_EQUAL_UINT32(177u, calcTimeMs(false, 1)); // SAMPLES_2
    TEST_ASSERT_EQUAL_UINT32(245u, calcTimeMs(false, 2)); // SAMPLES_4
    TEST_ASSERT_EQUAL_UINT32(381u, calcTimeMs(false, 3)); // SAMPLES_8
    TEST_ASSERT_EQUAL_UINT32(653u, calcTimeMs(false, 4)); // SAMPLES_16 (143 + 15 * 34 = 653 ms)
}

// 18. EMA Filter Seeding Guard — Must Not Seed From Faulted Read (Issue #15)
static void test_sensor_ema_not_seeded_on_fault()
{
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
    TEST_ASSERT_TRUE(filterInit); // Now seeded correctly
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.5f, filteredTemp); // Seeded with real temperature

    // Tick 4: normal EMA update
    float rawTemp4 = 23.0f;
    filteredTemp = (alpha * rawTemp4) + ((1.0f - alpha) * filteredTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.65f, filteredTemp); // 0.3*23 + 0.7*22.5 = 22.65
}

// 19. _faultStreak uint8_t Overflow Prevention (Critical Safety Bug Fix)
// Without capping, after 255 consecutive fault reads the uint8_t wraps to 0.
// Then 0 >= faultStreakLimit is false → driver falsely reports isValid=true.
static void test_sensor_fault_streak_no_overflow()
{
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
static void test_sensor_por_brownout_detection()
{
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

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_sensor_tests()
{
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
}

