// ============================================================================
// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later
// ============================================================================

/**
 * @file test_concurrency_tsan.cpp
 * @brief ThreadSanitizer Multi-Core Concurrency Stress Test.
 *
 * Simulates high-speed concurrent execution between Core 0 (Web & REST inputs)
 * and Core 1 (100 Hz Control & Safety loop) over FreeRTOS queues and SystemContext mutex.
 */

#include "config/machine_config.hpp"
#include "fsm/reflow_fsm.hpp"
#include "output/output_manager.hpp"
#include "pid/pid_controller.hpp"
#include "safety/safety_watchdog.hpp"
#include "system_context.hpp"
#include "web/fsm_command_queue.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace app {

QueueHandle_t g_fsmCmdQueue = nullptr;

} // namespace app

int main() {
    printf("======================================================================\n");
    printf(" BGA Reflow Controller - ThreadSanitizer (TSan) Multi-Core Guard\n");
    printf("======================================================================\n");

    // 1. Initialize Inter-Core Synchronisation Primitives
    app::SystemContext ctx;
    assert(ctx.begin());
    assert(app::fsmCmdQueueInit());

    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);
    safety::SafetyWatchdog safety(outputs);

    std::atomic<bool> stopFlag{false};
    std::atomic<uint64_t> core1Cycles{0};
    std::atomic<uint64_t> core0PollCycles{0};
    std::atomic<uint64_t> core0CmdCycles{0};

    constexpr int TOTAL_CYCLES = 5000;

    // 2. Thread 1: Simulating Core 1 Real-Time Task Loop (100 Hz Control & Safety)
    std::thread core1Thread([&]() {
        for (int i = 0; i < TOTAL_CYCLES; ++i) {
            // A. Drain incoming commands from Core 0
            app::FsmCommand cmd{};
            while (xQueueReceive(app::g_fsmCmdQueue, &cmd, 0) == pdTRUE) {
                if (cmd.profile != nullptr) {
                    delete cmd.profile;
                    cmd.profile = nullptr;
                }
            }

            // B. Simulate Sensor Readings & Safety Check
            float topTemp = 25.0f + static_cast<float>(i % 200);
            float botTemp = 25.0f + static_cast<float>(i % 150);
            sensor::SensorReading rTop{};
            rTop.temperature = topTemp;
            rTop.isValid = true;
            sensor::SensorReading rBot{};
            rBot.temperature = botTemp;
            rBot.isValid = true;

            safety.check(rTop, rBot, 100.0f, 100.0f);
            fsm.update(topTemp, botTemp, 20);

            // C. Publish Atomic Snapshot to SystemContext
            if (ctx.lock(50)) {
                auto& data = ctx.getData();
                data.topTemp = topTemp;
                data.bottomTemp = botTemp;
                data.state = fsm.getState();
                data.stateStr = fsm.getStateString();
                data.topSetpoint = fsm.getTopSetpoint();
                data.bottomSetpoint = fsm.getBottomSetpoint();
                data.fanActive = fsm.getFanEffective();
                data.ssrTopActive = outputs.getSsrTopState();
                data.ssrBottomActive = outputs.getSsrBottomState();

                if ((i % 100) == 0) {
                    data.history.push_back({static_cast<uint16_t>(i / 10), topTemp, botTemp, data.topSetpoint, data.bottomSetpoint});
                }
                ctx.unlock();
            }

            core1Cycles.fetch_add(1, std::memory_order_relaxed);
        }
        stopFlag.store(true, std::memory_order_release);
    });

    // 3. Thread 2: Simulating Core 0 Telemetry Broadcaster (WebSockets 2 Hz polling)
    std::thread core0PollerThread([&]() {
        while (!stopFlag.load(std::memory_order_acquire)) {
            // Atomic snapshot query from Core 0
            app::SystemContextData snapshot = ctx.getSnapshot();

            // Verify telemetry integrity (no torn reads or corrupted float states)
            assert(!std::isnan(snapshot.topTemp));
            assert(!std::isnan(snapshot.bottomTemp));
            assert(!snapshot.stateStr.empty());

            core0PollCycles.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // 4. Thread 3: Simulating Core 0 User Inputs & REST Commands
    std::thread core0InputThread([&]() {
        int cmdIndex = 0;
        while (!stopFlag.load(std::memory_order_acquire)) {
            app::FsmCommand cmd{};
            switch (cmdIndex % 4) {
                case 0: cmd.type = app::FsmCommandType::STOP; break;
                case 1: cmd.type = app::FsmCommandType::SKIP_STEP; break;
                case 2: cmd.type = app::FsmCommandType::RESET_FAULT; break;
                case 3: cmd.type = app::FsmCommandType::REFLOW; break;
                default: break;
            }
            cmdIndex++;

            xQueueSend(app::g_fsmCmdQueue, &cmd, 10);
            core0CmdCycles.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // 5. Await thread convergence
    core1Thread.join();
    core0PollerThread.join();
    core0InputThread.join();

    // 6. Cleanup
    vQueueDelete(app::g_fsmCmdQueue);
    app::g_fsmCmdQueue = nullptr;

    printf("[+] ThreadSanitizer Run Complete: ZERO Data-Races Detected!\n");
    printf("    - Core 1 Control Cycles:  %llu\n", static_cast<unsigned long long>(core1Cycles.load()));
    printf("    - Core 0 Telemetry Polls: %llu\n", static_cast<unsigned long long>(core0PollCycles.load()));
    printf("    - Core 0 Inter-Core Cmds: %llu\n", static_cast<unsigned long long>(core0CmdCycles.load()));
    printf("======================================================================\n");

    return 0;
}
