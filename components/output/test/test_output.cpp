/**
 * @file test_output.cpp
 * @brief Unit tests for output manager and burst fire SSR PWM controller.
 * @author BGA Reflow Controller Team
 */

#include "unity.h"
#include "output/burst_fire.hpp"
#include "output/output_manager.hpp"

static void test_burst_fire_zero()
{
    output::BurstFire bf(1000);
    bf.begin(1000);
    bf.setPower(0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, bf.getPower());
    bf.update();
    TEST_ASSERT_FALSE(bf.getState());
    TEST_ASSERT_EQUAL_UINT32(0, bf.getOnTimeMs());
    TEST_ASSERT_EQUAL_UINT32(1000, bf.getOffTimeMs());
}

static void test_burst_fire_hundred()
{
    output::BurstFire bf(1000);
    bf.begin(1000);
    bf.setPower(100.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, bf.getPower());
    bf.update();
    TEST_ASSERT_TRUE(bf.getState());
    TEST_ASSERT_EQUAL_UINT32(1000, bf.getOnTimeMs());
    TEST_ASSERT_EQUAL_UINT32(0, bf.getOffTimeMs());
}

static void test_burst_fire_clamping()
{
    output::BurstFire bf(1000);
    bf.setPower(-25.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, bf.getPower());
    bf.setPower(150.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, bf.getPower());
}

static void test_burst_fire_window_clamping()
{
    output::BurstFire bf(50); // Below MIN_BURST_WINDOW_MS (100)
    TEST_ASSERT_EQUAL_UINT32(output::MIN_BURST_WINDOW_MS, bf.getWindowMs());

    bf.setWindowMs(25000); // Above MAX_BURST_WINDOW_MS (10000)
    TEST_ASSERT_EQUAL_UINT32(output::MAX_BURST_WINDOW_MS, bf.getWindowMs());
}

static void test_burst_fire_timing_calc()
{
    output::BurstFire bf(1000);
    bf.setPower(30.0f);
    TEST_ASSERT_EQUAL_UINT32(300, bf.getOnTimeMs());
    TEST_ASSERT_EQUAL_UINT32(700, bf.getOffTimeMs());

    bf.setPower(75.5f);
    TEST_ASSERT_EQUAL_UINT32(755, bf.getOnTimeMs());
    TEST_ASSERT_EQUAL_UINT32(245, bf.getOffTimeMs());
}

static void test_burst_fire_reset()
{
    output::BurstFire bf(1000);
    bf.setPower(60.0f);
    bf.update();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, bf.getPower());

    bf.reset();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, bf.getPower());
    TEST_ASSERT_FALSE(bf.getState());
}

static void test_output_safety_inhibit()
{
    output::OutputManager out;
    out.setInhibit(true);
    TEST_ASSERT_TRUE(out.isInhibited());
    out.setSsrTop(true);
    out.setSsrBottom(true);
    TEST_ASSERT_FALSE(out.getSsrTopState());
    TEST_ASSERT_FALSE(out.getSsrBottomState());
    out.setInhibit(false);
    TEST_ASSERT_FALSE(out.isInhibited());
}

static void test_output_auxiliary_channels()
{
    output::OutputManager out;

    // Initial state check
    TEST_ASSERT_FALSE(out.getFanState());
    TEST_ASSERT_FALSE(out.getLampState());
    TEST_ASSERT_FALSE(out.getBuzzerState());

    // Toggle Fan
    out.setFan(true);
    TEST_ASSERT_TRUE(out.getFanState());
    out.setFan(false);
    TEST_ASSERT_FALSE(out.getFanState());

    // Toggle Lamp
    out.setLamp(true);
    TEST_ASSERT_TRUE(out.getLampState());
    out.setLamp(false);
    TEST_ASSERT_FALSE(out.getLampState());

    // Toggle Buzzer
    out.setBuzzer(true);
    TEST_ASSERT_TRUE(out.getBuzzerState());
    out.setBuzzer(false);
    TEST_ASSERT_FALSE(out.getBuzzerState());
}

static void test_output_channel_polarity()
{
    output::OutputManager out;
    out.setChannelPolarity(config::PinConfig::FAN, false); // Active LOW
    out.setFan(true);
    TEST_ASSERT_TRUE(out.getFanState());
    out.setChannelPolarity(config::PinConfig::FAN, true); // Active HIGH
}

void run_output_tests()
{
    RUN_TEST(test_burst_fire_zero);
    RUN_TEST(test_burst_fire_hundred);
    RUN_TEST(test_burst_fire_clamping);
    RUN_TEST(test_burst_fire_window_clamping);
    RUN_TEST(test_burst_fire_timing_calc);
    RUN_TEST(test_burst_fire_reset);
    RUN_TEST(test_output_safety_inhibit);
    RUN_TEST(test_output_auxiliary_channels);
    RUN_TEST(test_output_channel_polarity);
}

