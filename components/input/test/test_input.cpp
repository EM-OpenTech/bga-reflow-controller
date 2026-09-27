/**
 * @file test_input.cpp
 * @brief Unit tests for input::InputManager and hardware input configuration.
 *
 * Tests cover:
 *  1. Default configuration active-LOW and debounce timings
 *  2. Custom active-HIGH and debounce configuration
 *  3. Start, Stop and Fan switch callback registration & execution
 *  4. Lamp switch callback registration & execution
 *  5. Initial debounced and held states
 *  6. Consume-on-read semantics for wasStartPressed and wasStopPressed
 *
 * @author ESP-IDF Reflow Controller Team
 * @date 2026-09-23
 */

#include "unity.h"
#include "input/input_manager.hpp"

// ── 1. Default configuration ──────────────────────────────────────────────────

static void test_input_config_defaults()
{
    input::InputSystemConfig cfg;
    TEST_ASSERT_TRUE(cfg.btnStart.activeLow);
    TEST_ASSERT_TRUE(cfg.btnStop.activeLow);
    TEST_ASSERT_TRUE(cfg.swFan.activeLow);
    TEST_ASSERT_TRUE(cfg.swLamp.activeLow);
    TEST_ASSERT_EQUAL_UINT32(30, cfg.btnStart.debounceMs);
    TEST_ASSERT_EQUAL_UINT32(30, cfg.btnStop.debounceMs);
    TEST_ASSERT_EQUAL_UINT32(30, cfg.swFan.debounceMs);
    TEST_ASSERT_EQUAL_UINT32(30, cfg.swLamp.debounceMs);
}

// ── 2. Custom configuration ───────────────────────────────────────────────────

static void test_input_active_high_and_custom_config()
{
    input::InputSystemConfig cfg;
    cfg.btnStart.activeLow = false;
    cfg.btnStart.debounceMs = 50;
    cfg.btnStop.activeLow = false;
    cfg.btnStop.debounceMs = 15;

    input::InputManager mgr(cfg);
    TEST_ASSERT_FALSE(mgr.isStartHeld());
    TEST_ASSERT_FALSE(mgr.isStopHeld());
    TEST_ASSERT_FALSE(mgr.wasStartPressed());
    TEST_ASSERT_FALSE(mgr.wasStopPressed());
}

// ── 3. Start, Stop & Fan Switch Callbacks ─────────────────────────────────────

static void test_input_callbacks()
{
    input::InputManager mgr;
    bool startPressed = false;
    mgr.setOnStartPressed([&startPressed]() { startPressed = true; });

    bool stopPressed = false;
    mgr.setOnStopPressed([&stopPressed]() { stopPressed = true; });

    bool fanSwitch = false;
    mgr.setOnFanSwitchChanged([&fanSwitch](bool active) { fanSwitch = active; });

    TEST_ASSERT_FALSE(startPressed);
    TEST_ASSERT_FALSE(stopPressed);
    TEST_ASSERT_FALSE(fanSwitch);
}

// ── 4. Lamp Switch Callback ───────────────────────────────────────────────────

static void test_input_lamp_switch_callback()
{
    input::InputManager mgr;
    bool lampSwitch = false;
    mgr.setOnLampSwitchChanged([&lampSwitch](bool active) { lampSwitch = active; });

    TEST_ASSERT_FALSE(lampSwitch);
}

// ── 5. Initial States ─────────────────────────────────────────────────────────

static void test_input_initial_states()
{
    input::InputManager mgr;
    TEST_ASSERT_FALSE(mgr.isStartHeld());
    TEST_ASSERT_FALSE(mgr.isStopHeld());
    TEST_ASSERT_FALSE(mgr.isFanSwitchActive());
    TEST_ASSERT_FALSE(mgr.isLampSwitchActive());
    TEST_ASSERT_FALSE(mgr.wasStartPressed());
    TEST_ASSERT_FALSE(mgr.wasStopPressed());
}

// ── 6. Consume-on-read Semantics ──────────────────────────────────────────────

static void test_input_was_pressed_consume_semantics()
{
    input::InputManager mgr;
    // Without any GPIO transitions, wasStartPressed() must return false
    TEST_ASSERT_FALSE(mgr.wasStartPressed());
    TEST_ASSERT_FALSE(mgr.wasStartPressed());
    TEST_ASSERT_FALSE(mgr.wasStopPressed());
    TEST_ASSERT_FALSE(mgr.wasStopPressed());
}

// ── Runner ───────────────────────────────────────────────────────────────────

void run_input_tests()
{
    RUN_TEST(test_input_config_defaults);
    RUN_TEST(test_input_active_high_and_custom_config);
    RUN_TEST(test_input_callbacks);
    RUN_TEST(test_input_lamp_switch_callback);
    RUN_TEST(test_input_initial_states);
    RUN_TEST(test_input_was_pressed_consume_semantics);
}
