// ============================================================================
// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later
// ============================================================================

#include "unity.h"
#include <cstdio>

// ============================================================================
// External Test Suite Entry Points
// ============================================================================

extern void run_config_tests();
extern void run_output_tests();
extern void run_safety_tests();
extern void run_pid_tests();
extern void run_autotuner_tests();
extern void run_fsm_tests();
extern void run_simulation_tests();
extern void run_sensor_tests();
extern void run_storage_tests();
extern void run_web_tests();

void setUp() {}
void tearDown() {}

// ============================================================================
// Host Mocks for Embedded Binary Web Assets
// ============================================================================

#ifndef ESP_PLATFORM
extern const uint8_t mock_index_html_gz_start[] asm("_binary_index_html_gz_start") = {0};
extern const uint8_t mock_index_html_gz_end[] asm("_binary_index_html_gz_end") = {0};
extern const uint8_t mock_style_css_gz_start[] asm("_binary_style_css_gz_start") = {0};
extern const uint8_t mock_style_css_gz_end[] asm("_binary_style_css_gz_end") = {0};
extern const uint8_t mock_app_js_gz_start[] asm("_binary_app_js_gz_start") = {0};
extern const uint8_t mock_app_js_gz_end[] asm("_binary_app_js_gz_end") = {0};
extern const uint8_t mock_chart_min_js_gz_start[] asm("_binary_chart_min_js_gz_start") = {0};
extern const uint8_t mock_chart_min_js_gz_end[] asm("_binary_chart_min_js_gz_end") = {0};
extern const uint8_t mock_de_js_gz_start[] asm("_binary_de_js_gz_start") = {0};
extern const uint8_t mock_de_js_gz_end[] asm("_binary_de_js_gz_end") = {0};
#endif

// ============================================================================
// Main Entry Point
// ============================================================================

int main() {
    printf("======================================================================\n");
    printf(" BGA Reflow Controller - Native Host Test Suite (ASan & UBSan Active)\n");
    printf("======================================================================\n\n");

    UNITY_BEGIN();

    printf("--> [1/10] Running Config & Pin Allocation Tests...\n");
    run_config_tests();

    printf("\n--> [2/10] Running Output & SSR BurstFire Tests...\n");
    run_output_tests();

    printf("\n--> [3/10] Running Safety Watchdog & Inhibit Tests...\n");
    run_safety_tests();

    printf("\n--> [4/10] Running PID Controller Tests...\n");
    run_pid_tests();

    printf("\n--> [5/10] Running PID Autotuner Tests...\n");
    run_autotuner_tests();

    printf("\n--> [6/10] Running Reflow FSM & StepRunner Tests...\n");
    run_fsm_tests();

    printf("\n--> [7/10] Running Thermal Simulator Tests...\n");
    run_simulation_tests();

    printf("\n--> [8/10] Running MAX31856 Sensor Tests...\n");
    run_sensor_tests();

    printf("\n--> [9/10] Running LittleFS Storage Tests...\n");
    run_storage_tests();

    printf("\n--> [10/10] Running Web, Telemetry, CORS & Security Tests...\n");
    run_web_tests();

    printf("\n======================================================================\n");
    int failures = UNITY_END();
    printf("======================================================================\n");

    return failures;
}
