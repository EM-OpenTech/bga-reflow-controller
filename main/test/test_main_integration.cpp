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
 * @file test_main_integration.cpp
 * @brief Integration tests for main application controller, context, and FSM command queue.
 *
 * Tests SystemContext initialization, atomic locking and data mutations,
 * firmware app descriptor validation, and FSM cross-core command queue lifecycle.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "unity.h"
#include "system_context.hpp"
#include "web/fsm_command_queue.hpp"
#include "esp_app_desc.h"
#include "esp_task_wdt.h"

// 1. SystemContext Defaults & Initialization Test
static void test_main_system_context_init()
{
    app::SystemContext ctx;
    app::SystemContextData snap = ctx.getSnapshot();

    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::IDLE, (uint8_t)snap.state);
    TEST_ASSERT_EQUAL_STRING("IDLE", snap.stateStr.c_str());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, snap.topTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, snap.bottomTemp);
    TEST_ASSERT_TRUE(snap.topSensorOk);
    TEST_ASSERT_TRUE(snap.bottomSensorOk);
    TEST_ASSERT_FALSE(snap.preheatDone);
    TEST_ASSERT_FALSE(snap.fanActive);
    TEST_ASSERT_FALSE(snap.lampActive);
    TEST_ASSERT_EQUAL_UINT32(0, snap.history.size());
    TEST_ASSERT_EQUAL_UINT32(0, snap.stepMarkers.size());
}

// 2. SystemContext Locking & Data Mutations Test
static void test_main_system_context_lock_mutation()
{
    app::SystemContext ctx;
    if (ctx.lock(100)) {
        auto& data = ctx.getData();
        data.topTemp = 185.5f;
        data.bottomTemp = 175.2f;
        data.state = fsm::ReflowState::SOAK;
        data.stateStr = "SOAK";
        data.fanActive = true;
        data.history.push_back({10, 185.5f, 175.2f, 190.0f, 180.0f});
        ctx.unlock();
    }

    app::SystemContextData snap = ctx.getSnapshot();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 185.5f, snap.topTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 175.2f, snap.bottomTemp);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::SOAK, (uint8_t)snap.state);
    TEST_ASSERT_EQUAL_STRING("SOAK", snap.stateStr.c_str());
    TEST_ASSERT_TRUE(snap.fanActive);
    TEST_ASSERT_EQUAL_UINT32(1, snap.history.size());
    TEST_ASSERT_EQUAL_UINT16(10, snap.history[0].timeS);
}

// 3. Firmware Version & Application Descriptor Test
static void test_main_firmware_version_descriptor()
{
    const esp_app_desc_t* desc = esp_app_get_description();
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_NOT_NULL(desc->version);
#ifdef PROJECT_VER
    TEST_ASSERT_EQUAL_STRING(PROJECT_VER, desc->version);
#endif
    TEST_ASSERT_GREATER_THAN_UINT32(0, strlen(desc->version));
    TEST_ASSERT_EQUAL_STRING("bga_reflow_controller", desc->project_name);
    TEST_ASSERT_NOT_NULL(desc->idf_ver);
}

// 4. Cross-Core FSM Command Queue Lifecycle Test
static void test_main_fsm_command_queue_operations()
{
    // Initialize command queue
    TEST_ASSERT_TRUE(app::fsmCmdQueueInit());
    TEST_ASSERT_NOT_NULL(app::g_fsmCmdQueue);

    // Drain any stale commands if queue had items
    app::FsmCommand dummyCmd;
    while (xQueueReceive(app::g_fsmCmdQueue, &dummyCmd, 0) == pdTRUE) {
        if (dummyCmd.profile != nullptr) {
            delete dummyCmd.profile;
        }
    }

    // Test posting and receiving standard commands
    app::FsmCommand cmd1 = {app::FsmCommandType::REFLOW, nullptr, false, 0.0f};
    TEST_ASSERT_TRUE(app::fsmCmdPost(cmd1));

    app::FsmCommand cmd2 = {app::FsmCommandType::AUTOTUNE_START, nullptr, true, 160.0f};
    TEST_ASSERT_TRUE(app::fsmCmdPost(cmd2));

    app::FsmCommand recvCmd = {};
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(app::g_fsmCmdQueue, &recvCmd, 0));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)app::FsmCommandType::REFLOW, (uint8_t)recvCmd.type);

    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(app::g_fsmCmdQueue, &recvCmd, 0));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)app::FsmCommandType::AUTOTUNE_START, (uint8_t)recvCmd.type);
    TEST_ASSERT_TRUE(recvCmd.tuneIsTop);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, recvCmd.tuneTargetTemp);

    // Test Preheat command with dynamic profile pointer
    auto* testProf = new config::ReflowProfile();
    testProf->name = "QueueTestProfile";
    app::FsmCommand cmdPreheat = {app::FsmCommandType::PREHEAT, testProf, false, 0.0f};
    TEST_ASSERT_TRUE(app::fsmCmdPost(cmdPreheat));

    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(app::g_fsmCmdQueue, &recvCmd, 0));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)app::FsmCommandType::PREHEAT, (uint8_t)recvCmd.type);
    TEST_ASSERT_NOT_NULL(recvCmd.profile);
    TEST_ASSERT_EQUAL_STRING("QueueTestProfile", recvCmd.profile->name.c_str());
    delete recvCmd.profile; // Consumer cleanup simulation
}

// 5. MachineSettings to PID & Sensor Parameters Binding Test
static void test_main_settings_parameter_binding()
{
    config::MachineSettings s;
    // Verify default PID constants in settings
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, s.topKp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.05f, s.topKi);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, s.topKd);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, s.bottomKp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.04f, s.bottomKi);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, s.bottomKd);

    // Verify default CJ offsets
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.topCjOffset);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.bottomCjOffset);

    // Verify that modified values validate correctly
    s.topCjOffset = -2.5f;
    s.bottomCjOffset = 1.8f;
    TEST_ASSERT_TRUE(s.validate().valid);
}

// 6. ESP-IDF Task Watchdog Timer (TWDT) Configuration & Lifecycle Test
static void test_main_task_watchdog_lifecycle()
{
#if CONFIG_ESP_TASK_WDT_EN
    // 1. Verify TWDT Timeout configuration matches architecture (5 seconds)
    TEST_ASSERT_EQUAL_INT(5, CONFIG_ESP_TASK_WDT_TIMEOUT_S);

    // 2. Test dynamic subscription, reset and deletion on current task
    esp_err_t statusBefore = esp_task_wdt_status(NULL);

    if (statusBefore == ESP_ERR_NOT_FOUND) {
        // Subscribe to TWDT
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_add(NULL));
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_status(NULL));

        // Feed/reset TWDT
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_reset());

        // Unsubscribe from TWDT
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_delete(NULL));
        TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, esp_task_wdt_status(NULL));
    } else if (statusBefore == ESP_OK) {
        // Already subscribed: verify feeding works cleanly
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_reset());
    }
#else
    TEST_IGNORE_MESSAGE("TWDT not enabled in sdkconfig");
#endif
}

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_main_integration_tests()
{
    RUN_TEST(test_main_system_context_init);
    RUN_TEST(test_main_system_context_lock_mutation);
    RUN_TEST(test_main_firmware_version_descriptor);
    RUN_TEST(test_main_fsm_command_queue_operations);
    RUN_TEST(test_main_settings_parameter_binding);
    RUN_TEST(test_main_task_watchdog_lifecycle);
}


