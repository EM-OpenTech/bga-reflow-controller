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
 * @file test_config.cpp
 * @brief Unit tests for configuration constants, hardware pins, schemas and validation.
 *
 * Tests cover:
 *  1. Pin allocation conflict check (unique GPIO numbers)
 *  2. Default MachineSettings parameters
 *  3. Schema version validity
 *  4. MachineSettings domain validation bounds
 *  5. ReflowProfile and ProfileStep validation
 *  6. PidLibrary and PidPoint validation
 *  7. Buzzer duration constants
 *  8. Resolution and step size constants
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "unity.h"
#include "config/pin_config.hpp"
#include "config/machine_config.hpp"
#include <set>

// ── 1. Pin Conflict Test ──────────────────────────────────────────────────────

static void test_config_pin_conflicts()
{
    using namespace config;
    std::set<gpio_num_t> pins = {
        PinConfig::SPI_SCK, PinConfig::SPI_MISO, PinConfig::SPI_MOSI,
        PinConfig::CS_TOP, PinConfig::CS_BOTTOM,
        PinConfig::SSR_TOP, PinConfig::SSR_BOTTOM,
        PinConfig::FAN, PinConfig::LAMP, PinConfig::BUZZER,
        PinConfig::BTN_START, PinConfig::BTN_STOP,
        PinConfig::SW_FAN, PinConfig::SW_LAMP
    };
    TEST_ASSERT_EQUAL_INT(14, pins.size());
}

// ── 2. Default Values Test ────────────────────────────────────────────────────

static void test_config_default_values()
{
    config::MachineSettings settings;
    TEST_ASSERT_TRUE(settings.simulationMode);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 280.0f, settings.maxTempTop);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 280.0f, settings.maxTempBottom);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 45.0f, settings.coolingSafeTemp);
    TEST_ASSERT_EQUAL_UINT32(1000, settings.topBurstWindowMs);
    TEST_ASSERT_EQUAL_UINT32(1000, settings.bottomBurstWindowMs);
    TEST_ASSERT_EQUAL_STRING("en", settings.language.c_str());
    TEST_ASSERT_EQUAL_STRING("light", settings.theme.c_str());
}

// ── 3. Schema Versions Test ───────────────────────────────────────────────────

static void test_config_schema_versions()
{
    // Schema versions must be positive defined constants (> 0)
    TEST_ASSERT_GREATER_THAN_UINT16(0, config::Schema::MACHINE_SETTINGS);
    TEST_ASSERT_GREATER_THAN_UINT16(0, config::Schema::REFLOW_PROFILE);
    TEST_ASSERT_GREATER_THAN_UINT16(0, config::Schema::PID_LIBRARY);
}

// ── 4. Machine Settings Validation Test ────────────────────────────────────────

static void test_config_settings_validation()
{
    config::MachineSettings settings;
    // Default settings must be 100% valid
    auto res = settings.validate();
    TEST_ASSERT_TRUE(res.valid);
    TEST_ASSERT_NULL(res.errorMessage);

    // Invalid max temperature
    settings.maxTempTop = 350.0f;
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.maxTempTop = 280.0f;

    // Min exceeds Max
    settings.minTempTop = 290.0f;
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.minTempTop = 5.0f;

    // Invalid cooling safe temp
    settings.coolingSafeTemp = 10.0f; // min is 20
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.coolingSafeTemp = 45.0f;

    // Invalid EMA alpha
    settings.emaAlpha = 1.5f;
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.emaAlpha = 0.3f;

    // SSR Burst-Fire Window (500 .. 4000 ms)
    settings.topBurstWindowMs = 100; // < 500 ms must fail
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.topBurstWindowMs = 5000; // > 4000 ms must fail
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.topBurstWindowMs = 1000;

    settings.bottomBurstWindowMs = 499; // < 500 ms must fail
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.bottomBurstWindowMs = 1000;

    // FSM Settle duration (1 .. 60 s)
    settings.settleTimeS = 0; // < 1 s must fail
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.settleTimeS = 61; // > 60 s must fail
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.settleTimeS = 5;

    // FSM Hold Tolerances (0.5 .. 30 °C)
    settings.holdLowTolerance = 0.2f;
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.holdLowTolerance = 2.0f;

    // Fan Timings (Delay: 0..300s, Duration: 10..600s)
    settings.fanCoolingDurationS = 5; // < 10s must fail
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.fanCoolingDurationS = 60;

    // Stuck SSR Watchdog
    settings.enableStuckSsrCheck = true;
    settings.stuckSsrRiseThreshold = 0.5f; // < 1.0°C must fail
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.stuckSsrRiseThreshold = 5.0f;

    // No-Rise Watchdog
    settings.enableNoRiseCheck = true;
    settings.noRiseTimeoutSec = 3; // < 5s must fail
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.noRiseTimeoutSec = 45;

    // Sensor Calibration Offsets (-20 .. +20 °C)
    settings.topCjOffset = 25.0f; // > +20 must fail
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.topCjOffset = 0.0f;

    // Button debounce (5 .. 500 ms)
    settings.btnDebounceMs = 2; // < 5 ms must fail
    TEST_ASSERT_FALSE(settings.validate().valid);
    settings.btnDebounceMs = 30;

    // Final sanity check
    TEST_ASSERT_TRUE(settings.validate().valid);
}

// ── 5. Reflow Profile Validation Test ─────────────────────────────────────────

static void test_config_profile_validation()
{
    config::ReflowProfile profile;
    profile.name = "Test Profile";
    profile.file = "test.json";

    // Empty steps must fail
    TEST_ASSERT_FALSE(profile.validate().valid);

    // Add valid top step
    config::ProfileStep step1;
    step1.temp = 150.0f;
    step1.time = 30;
    step1.ramp = 1.5f;
    TEST_ASSERT_TRUE(step1.validate().valid);
    profile.stepsTop.push_back(step1);

    // Now profile is valid
    TEST_ASSERT_TRUE(profile.validate().valid);

    // Name too long (>30 chars)
    profile.name = "This is a very long profile name that exceeds 30 characters";
    TEST_ASSERT_FALSE(profile.validate().valid);
    profile.name = "Valid Name";

    // Path traversal in filename
    profile.file = "../evil.json";
    TEST_ASSERT_FALSE(profile.validate().valid);
    profile.file = "valid.json";

    // Invalid step temp (< 30°C or > 300°C)
    config::ProfileStep badStep;
    badStep.temp = 20.0f; // min is 30
    profile.stepsBottom.push_back(badStep);
    TEST_ASSERT_FALSE(profile.validate().valid);
    profile.stepsBottom.clear();

    // Step limit (>10 steps)
    for (int i = 0; i < 11; i++) {
        profile.stepsTop.push_back(step1);
    }
    TEST_ASSERT_FALSE(profile.validate().valid);
}

// ── 6. PID Library Validation Test ───────────────────────────────────────────

static void test_config_pid_validation()
{
    config::PidLibrary lib;
    // Empty library must fail
    TEST_ASSERT_FALSE(lib.validate().valid);

    config::PidPoint pt;
    pt.temp = 150.0f;
    pt.kp = 2.0f;
    pt.ki = 0.05f;
    pt.kd = 1.0f;
    TEST_ASSERT_TRUE(pt.validate().valid);

    lib.top.push_back(pt);
    TEST_ASSERT_TRUE(lib.validate().valid);

    // Invalid point (<30°C)
    config::PidPoint badPt = pt;
    badPt.temp = 10.0f;
    TEST_ASSERT_FALSE(badPt.validate().valid);

    lib.bottom.push_back(badPt);
    TEST_ASSERT_FALSE(lib.validate().valid);
}

// ── 7. Buzzer Constants Test ──────────────────────────────────────────────────

static void test_config_buzzer_constants()
{
    TEST_ASSERT_GREATER_THAN_UINT32(0, config::Buzzer::PREHEAT_DONE_BEEP_MS);
    TEST_ASSERT_GREATER_THAN_UINT32(0, config::Buzzer::REFLOW_DONE_BEEP_MS);
    TEST_ASSERT_EQUAL_UINT32(1000, config::Buzzer::PREHEAT_DONE_BEEP_MS);
    TEST_ASSERT_EQUAL_UINT32(3000, config::Buzzer::REFLOW_DONE_BEEP_MS);
}

// ── 8. Resolution Constants Test ──────────────────────────────────────────────

static void test_config_resolution_constants()
{
    TEST_ASSERT_TRUE(config::Resolution::TEMPERATURE > 0.0f);
    TEST_ASSERT_TRUE(config::Resolution::RAMP_RATE > 0.0f);
    TEST_ASSERT_TRUE(config::Resolution::PID_GAIN_KP > 0.0f);
    TEST_ASSERT_TRUE(config::Resolution::PID_GAIN_KI > 0.0f);
    TEST_ASSERT_TRUE(config::Resolution::PID_GAIN_KD > 0.0f);
    TEST_ASSERT_TRUE(config::Resolution::FILTER_ALPHA > 0.0f);
    TEST_ASSERT_TRUE(config::Resolution::OFFSET_TEMP > 0.0f);
    TEST_ASSERT_TRUE(config::Resolution::TOLERANCE_TEMP > 0.0f);
    TEST_ASSERT_TRUE(config::Resolution::TIME_SEC > 0);
    TEST_ASSERT_TRUE(config::Resolution::TIME_MS > 0);
}

// ── Runner ───────────────────────────────────────────────────────────────────

void run_config_tests()
{
    RUN_TEST(test_config_pin_conflicts);
    RUN_TEST(test_config_default_values);
    RUN_TEST(test_config_schema_versions);
    RUN_TEST(test_config_settings_validation);
    RUN_TEST(test_config_profile_validation);
    RUN_TEST(test_config_pid_validation);
    RUN_TEST(test_config_buzzer_constants);
    RUN_TEST(test_config_resolution_constants);
}

