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

#include "esp_app_desc.h"
#include "esp_task_wdt.h"
#include "system_context.hpp"
#include "unity.h"
#include "web/fsm_command_queue.hpp"

// 1. SystemContext Defaults & Initialization Test
static void test_main_system_context_init() {
    app::SystemContext ctx;
    app::SystemContextData snap = ctx.getSnapshot();

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(fsm::ReflowState::IDLE), static_cast<uint8_t>(snap.state));
    TEST_ASSERT_EQUAL_STRING("IDLE", snap.stateStr.c_str());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, snap.topTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, snap.bottomTemp);
    TEST_ASSERT_TRUE(snap.topSensorOk);
    TEST_ASSERT_TRUE(snap.bottomSensorOk);
    TEST_ASSERT_FALSE(snap.preheatDone);
    TEST_ASSERT_FALSE(snap.fanActive);
    TEST_ASSERT_FALSE(snap.lampActive);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.getHistory().size());
    TEST_ASSERT_EQUAL_UINT32(0, snap.stepMarkers.size());
}

// 2. SystemContext Locking & Data Mutations Test
static void test_main_system_context_lock_mutation() {
    app::SystemContext ctx;
    if (ctx.lock(100)) {
        auto& data = ctx.getData();
        data.topTemp = 185.5f;
        data.bottomTemp = 175.2f;
        data.state = fsm::ReflowState::SOAK;
        data.stateStr = "SOAK";
        data.fanActive = true;
        ctx.getHistory().push_back({10, 185.5f, 175.2f, 190.0f, 180.0f});
        ctx.unlock();
    }

    app::SystemContextData snap = ctx.getSnapshot();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 185.5f, snap.topTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 175.2f, snap.bottomTemp);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(fsm::ReflowState::SOAK), static_cast<uint8_t>(snap.state));
    TEST_ASSERT_EQUAL_STRING("SOAK", snap.stateStr.c_str());
    TEST_ASSERT_TRUE(snap.fanActive);
    TEST_ASSERT_EQUAL_UINT32(1, ctx.getHistory().size());
    TEST_ASSERT_EQUAL_UINT16(10, ctx.getHistory()[0].timeS);
}

// 3. Firmware Version & Application Descriptor Test
static void test_main_firmware_version_descriptor() {
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
static void test_main_fsm_command_queue_operations() {
    // Initialize command queue
    TEST_ASSERT_TRUE(app::fsmCmdQueueInit());
    TEST_ASSERT_NOT_NULL(app::g_fsmCmdQueue);

    // Drain any stale commands if queue had items
    app::FsmCommand dummyCmd;
    while (xQueueReceive(app::g_fsmCmdQueue, &dummyCmd, 0) == pdTRUE) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete dummyCmd.profile;
    }

    // Test posting and receiving standard commands
    app::FsmCommand cmd1 = {
        .type = app::FsmCommandType::REFLOW, .profile = nullptr, .tuneIsTop = false, .tuneTargetTemp = 0.0f};
    TEST_ASSERT_TRUE(app::fsmCmdPost(cmd1));

    app::FsmCommand cmd2 = {.type = app::FsmCommandType::AUTOTUNE_START,
                            .profile = nullptr,
                            .tuneIsTop = true,
                            .tuneTargetTemp = 160.0f,
                            .tuneMethod = 3};
    TEST_ASSERT_TRUE(app::fsmCmdPost(cmd2));

    app::FsmCommand recvCmd = {};
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(app::g_fsmCmdQueue, &recvCmd, 0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(app::FsmCommandType::REFLOW), static_cast<uint8_t>(recvCmd.type));

    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(app::g_fsmCmdQueue, &recvCmd, 0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(app::FsmCommandType::AUTOTUNE_START),
                            static_cast<uint8_t>(recvCmd.type));
    TEST_ASSERT_TRUE(recvCmd.tuneIsTop);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, recvCmd.tuneTargetTemp);
    TEST_ASSERT_EQUAL_UINT8(3, recvCmd.tuneMethod);

    // Test Preheat command with dynamic profile pointer
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* testProf = new config::ReflowProfile();
    testProf->name = "QueueTestProfile";
    app::FsmCommand cmdPreheat = {
        .type = app::FsmCommandType::PREHEAT, .profile = testProf, .tuneIsTop = false, .tuneTargetTemp = 0.0f};
    TEST_ASSERT_TRUE(app::fsmCmdPost(cmdPreheat));

    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(app::g_fsmCmdQueue, &recvCmd, 0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(app::FsmCommandType::PREHEAT), static_cast<uint8_t>(recvCmd.type));
    TEST_ASSERT_NOT_NULL(recvCmd.profile);
    TEST_ASSERT_EQUAL_STRING("QueueTestProfile", recvCmd.profile->name.c_str());
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete recvCmd.profile; // Consumer cleanup simulation
}

// 5. MachineSettings to PID & Sensor Parameters Binding Test
static void test_main_settings_parameter_binding() {
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
static void test_main_task_watchdog_lifecycle() {
#if CONFIG_ESP_TASK_WDT_EN
    // 1. Verify TWDT Timeout configuration matches architecture (5 seconds)
    TEST_ASSERT_EQUAL_INT(5, CONFIG_ESP_TASK_WDT_TIMEOUT_S);

    // 2. Test dynamic subscription, reset and deletion on current task
    esp_err_t statusBefore = esp_task_wdt_status(nullptr);

    if (statusBefore == ESP_ERR_NOT_FOUND) {
        // Subscribe to TWDT
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_add(nullptr));
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_status(nullptr));

        // Feed/reset TWDT
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_reset());

        // Unsubscribe from TWDT
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_delete(nullptr));
        TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, esp_task_wdt_status(nullptr));
    } else if (statusBefore == ESP_OK) {
        // Already subscribed: verify feeding works cleanly
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_reset());
    }
#else
    TEST_IGNORE_MESSAGE("TWDT not enabled in sdkconfig");
#endif
}

// 7. FSM Command Queue Overflow, ISR Posting & Edge Cases Test
static void test_main_fsm_command_queue_edge_cases() {
    // 1. Drain queue to empty
    app::FsmCommand dummyCmd;
    while (xQueueReceive(app::g_fsmCmdQueue, &dummyCmd, 0) == pdTRUE) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete dummyCmd.profile;
    }

    // 2. Test ISR posting
    app::FsmCommand isrCmd1{.type = app::FsmCommandType::STOP};
    TEST_ASSERT_TRUE(app::fsmCmdPostFromISR(isrCmd1));

    app::FsmCommand isrCmd2{.type = app::FsmCommandType::SKIP_STEP};
    TEST_ASSERT_TRUE(app::fsmCmdPostFromISR(isrCmd2));

    // 3. Fill queue to maximum capacity (depth 4: already 2 items in queue)
    app::FsmCommand cmd3{.type = app::FsmCommandType::ENTER_BACKUP};
    app::FsmCommand cmd4{.type = app::FsmCommandType::EXIT_BACKUP};
    TEST_ASSERT_TRUE(app::fsmCmdPost(cmd3));
    TEST_ASSERT_TRUE(app::fsmCmdPost(cmd4));

    // Queue is now completely FULL (4/4 items)
    // 4. Verify that 5th post fails gracefully (queue overflow rejection)
    app::FsmCommand overflowCmd{.type = app::FsmCommandType::REFLOW};
    TEST_ASSERT_FALSE(app::fsmCmdPost(overflowCmd));
    TEST_ASSERT_FALSE(app::fsmCmdPostFromISR(overflowCmd));

    // 5. Drain and verify all 4 commands in exact FIFO order
    app::FsmCommand r1{}, r2{}, r3{}, r4{};
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(app::g_fsmCmdQueue, &r1, 0));
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(app::g_fsmCmdQueue, &r2, 0));
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(app::g_fsmCmdQueue, &r3, 0));
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(app::g_fsmCmdQueue, &r4, 0));

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(app::FsmCommandType::STOP), static_cast<uint8_t>(r1.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(app::FsmCommandType::SKIP_STEP), static_cast<uint8_t>(r2.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(app::FsmCommandType::ENTER_BACKUP), static_cast<uint8_t>(r3.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(app::FsmCommandType::EXIT_BACKUP), static_cast<uint8_t>(r4.type));

    // 6. Verify queue is empty now
    TEST_ASSERT_EQUAL(pdFALSE, xQueueReceive(app::g_fsmCmdQueue, &dummyCmd, 0));

    // 7. Test uninitialized queue rejection (simulate g_fsmCmdQueue == nullptr)
    QueueHandle_t savedQueue = app::g_fsmCmdQueue;
    app::g_fsmCmdQueue = nullptr;

    TEST_ASSERT_FALSE(app::fsmCmdPost(overflowCmd));
    TEST_ASSERT_FALSE(app::fsmCmdPostFromISR(overflowCmd));

    // Restore queue handle
    app::g_fsmCmdQueue = savedQueue;
}

// 8. SystemContext Re-entrancy, Const-Correctness & Edge Cases Test
static void test_main_system_context_edge_cases() {
    app::SystemContext ctx;
    // 1. begin() call on initialized context succeeds
    TEST_ASSERT_TRUE(ctx.begin());

    // 2. Multiple locks and unlocks
    TEST_ASSERT_TRUE(ctx.lock(50));
    ctx.getData().topTemp = 200.0f;
    ctx.unlock();

    // 3. Const access
    const app::SystemContext& constCtx = ctx;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, constCtx.getData().topTemp);

    // 4. Multiple unlock calls must not crash
    ctx.unlock();
}

// 9. Time-Warp Soak / Duration Stress Test (3 Hours / 10,800 Seconds Simulation)
static void test_main_soak_duration_simulation() {
    app::SystemContext ctx;
    TEST_ASSERT_TRUE(ctx.begin());

    // 1. Simulate 10,800 process seconds (3 hours) with 1 Hz history recording
    // and 2 Hz live WebSocket snapshot extraction (21,600 snapshots)
    constexpr uint32_t TOTAL_DURATION_SEC = 10800;

    for (uint32_t sec = 0; sec < TOTAL_DURATION_SEC; ++sec) {
        // Core 1 Simulation: Record 1 history point per second under lock
        if (ctx.lock(10)) {
            auto& data = ctx.getData();
            data.elapsedSec = sec;
            data.topTemp = 25.0f + (static_cast<float>(sec % 300) * 0.5f);
            data.bottomTemp = 25.0f + (static_cast<float>(sec % 300) * 0.4f);
            data.topSetpoint = 150.0f;
            data.bottomSetpoint = 140.0f;
            data.state = fsm::ReflowState::SOAK;

            if (ctx.getHistory().size() < config::Limits::MAX_HISTORY_POINTS) {
                ctx.getHistory().push_back(
                    {static_cast<uint16_t>(sec), data.topTemp, data.bottomTemp, data.topSetpoint, data.bottomSetpoint});
            }
            ctx.unlock();
        }

        // Core 0 Simulation: Extract 2 Hz live snapshots (every 500 ms)
        // Must be non-allocating, zero memory leaks, and blazing fast
        const app::SystemContextData snap1 = ctx.getSnapshot();
        const app::SystemContextData snap2 = ctx.getSnapshot();
        TEST_ASSERT_EQUAL_UINT32(snap1.elapsedSec, snap2.elapsedSec);

        // Sample check invariants at key milestones across the entire 3-hour duration
        if (sec == 60) {
            TEST_ASSERT_EQUAL_UINT32(61, ctx.getHistory().size());
            TEST_ASSERT_EQUAL_UINT32(60, snap1.elapsedSec);
        } else if (sec == 240) { // The exact second where previous TWDT timeout occurred
            TEST_ASSERT_EQUAL_UINT32(241, ctx.getHistory().size());
            TEST_ASSERT_EQUAL_UINT32(240, snap1.elapsedSec);
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(fsm::ReflowState::SOAK), static_cast<uint8_t>(snap1.state));
        } else if (sec == 1800) { // 30 minutes
            TEST_ASSERT_EQUAL_UINT32(1801, ctx.getHistory().size());
            TEST_ASSERT_EQUAL_UINT32(1800, snap1.elapsedSec);
        } else if (sec == 3600) { // 1 hour
            TEST_ASSERT_EQUAL_UINT32(3601, ctx.getHistory().size());
            TEST_ASSERT_EQUAL_UINT32(3600, snap1.elapsedSec);
        } else if (sec == 7200) { // 2 hours
            TEST_ASSERT_EQUAL_UINT32(7201, ctx.getHistory().size());
            TEST_ASSERT_EQUAL_UINT32(7200, snap1.elapsedSec);
        }
    }

    // 2. Verify strict capacity cap at 10,800 points (zero buffer overflow)
    TEST_ASSERT_EQUAL_UINT32(config::Limits::MAX_HISTORY_POINTS, ctx.getHistory().size());

    // 3. Verify final snapshot integrity after 3 simulated hours
    app::SystemContextData finalSnap = ctx.getSnapshot();
    TEST_ASSERT_EQUAL_UINT32(TOTAL_DURATION_SEC - 1, finalSnap.elapsedSec);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(fsm::ReflowState::SOAK), static_cast<uint8_t>(finalSnap.state));
}

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_main_integration_tests() {
    RUN_TEST(test_main_system_context_init);
    RUN_TEST(test_main_system_context_lock_mutation);
    RUN_TEST(test_main_firmware_version_descriptor);
    RUN_TEST(test_main_fsm_command_queue_operations);
    RUN_TEST(test_main_settings_parameter_binding);
    RUN_TEST(test_main_task_watchdog_lifecycle);
    RUN_TEST(test_main_fsm_command_queue_edge_cases);
    RUN_TEST(test_main_system_context_edge_cases);
    RUN_TEST(test_main_soak_duration_simulation);
}
