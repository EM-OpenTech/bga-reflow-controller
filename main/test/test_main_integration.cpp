#include "unity.h"
#include "system_context.hpp"
#include "web/fsm_command_queue.hpp"
#include "esp_app_desc.h"

static void test_main_system_context_init()
{
    app::SystemContext ctx;
    app::SystemContextData snap = ctx.getSnapshot();

    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::IDLE, (uint8_t)snap.state);
    TEST_ASSERT_EQUAL_STRING("IDLE", snap.stateStr.c_str());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, snap.topTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, snap.bottomTemp);
    TEST_ASSERT_FALSE(snap.preheatDone);
    TEST_ASSERT_FALSE(snap.fanActive);
    TEST_ASSERT_FALSE(snap.lampActive);
    TEST_ASSERT_EQUAL_UINT32(0, snap.history.size());
    TEST_ASSERT_EQUAL_UINT32(0, snap.stepMarkers.size());
}

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

void run_main_integration_tests()
{
    RUN_TEST(test_main_system_context_init);
    RUN_TEST(test_main_system_context_lock_mutation);
    RUN_TEST(test_main_firmware_version_descriptor);
    RUN_TEST(test_main_fsm_command_queue_operations);
}


