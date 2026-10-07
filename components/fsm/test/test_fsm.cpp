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
 * @file test_fsm.cpp
 * @brief Unit tests for Reflow Finite State Machine (fsm component).
 *
 * Tests state transitions, settle-gate logic, step markers, gain scheduling,
 * TAL calculation, buzzer pulses, fault triggers, skip step, and full profile simulation.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "unity.h"
#include "fsm/reflow_fsm.hpp"
#include "output/output_manager.hpp"
#include "pid/pid_controller.hpp"
#include "config/machine_config.hpp"
#include "simulation/thermal_simulator.hpp"

static constexpr uint32_t DT_MS = config::Timing::CONTROL_LOOP_PERIOD_MS;

// 1. Initial State & Invariant Checks Test
static void test_fsm_initial_state()
{
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::IDLE, (uint8_t)fsm.getState());
    TEST_ASSERT_EQUAL_STRING("IDLE", fsm.getStateString());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, fsm.getTopSetpoint());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, fsm.getBottomSetpoint());
    TEST_ASSERT_FALSE(fsm.isPreheatDone());
}

// 2. Start Preheat Transition Test
static void test_fsm_start_preheat()
{
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    config::ReflowProfile prof;
    config::ProfileStep step;
    step.temp = 150.0f;
    step.time = 30;
    step.ramp = 1.0f;
    prof.stepsBottom.push_back(step);
    prof.stepsTop.push_back(step);

    bool ok = fsm.startPreheat(prof);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::PREHEAT, (uint8_t)fsm.getState());
    TEST_ASSERT_EQUAL_STRING("PREHEAT", fsm.getStateString());
}

// 3. Process Abort / Stop Test
static void test_fsm_stop()
{
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    config::ReflowProfile prof;
    config::ProfileStep step;
    step.temp = 150.0f;
    step.time = 30;
    step.ramp = 1.0f;
    prof.stepsBottom.push_back(step);
    prof.stepsTop.push_back(step);

    fsm.startPreheat(prof);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::PREHEAT, (uint8_t)fsm.getState());
    fsm.stop();
    uint8_t st = (uint8_t)fsm.getState();
    TEST_ASSERT_TRUE(st == (uint8_t)fsm::ReflowState::COOLING || st == (uint8_t)fsm::ReflowState::IDLE);
}

// 4. Manual Fan & Lamp Overrides Test
static void test_fsm_overrides()
{
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    fsm.setFanOverride(true);
    TEST_ASSERT_TRUE(fsm.getFanEffective());
    fsm.setFanOverride(false);
    TEST_ASSERT_FALSE(fsm.getFanEffective());

    fsm.setLampOverride(true);
    TEST_ASSERT_TRUE(fsm.getLampEffective());
    fsm.setLampOverride(false);
    TEST_ASSERT_FALSE(fsm.getLampEffective());
}

// 5. Full Profile Thermal Simulation Test
static void test_fsm_full_profile_simulation()
{
    config::MachineSettings settings;
    settings.settleTimeS = 1; // Fast settle for unit test
    output::OutputManager outputs;
    pid::PIDController topPid(5.0f, 0.1f, 1.0f);
    pid::PIDController botPid(5.0f, 0.1f, 1.0f);
    topPid.begin();
    botPid.begin();
    topPid.setAutomatic(true);
    botPid.setAutomatic(true);

    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);
    sim::ThermalSimulator sim;
    sim.reset(25.0f);

    config::ReflowProfile prof;
    prof.name = "SAC305 Test";
    config::ProfileStep bStep;
    bStep.temp = 150.0f;
    bStep.time = 2;
    bStep.ramp = 5.0f;
    prof.stepsBottom.push_back(bStep);

    config::ProfileStep tStep;
    tStep.temp = 225.0f;
    tStep.time = 2;
    tStep.ramp = 5.0f;
    prof.stepsTop.push_back(tStep);

    // 1. Start Preheat
    TEST_ASSERT_TRUE(fsm.startPreheat(prof));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::PREHEAT, (uint8_t)fsm.getState());

    // 2. Simulate heating until preheatDone (25C -> 150C at 1.5C/s + ramp lag takes ~170s)
    for (int i = 0; i < 2500 && !fsm.isPreheatDone(); ++i) {
        fsm.update(sim.getTopTemperature(), sim.getBottomTemperature(), DT_MS);
        botPid.compute();
        topPid.compute();
        sim.update(topPid.getOutput(), botPid.getOutput(), fsm.getFanEffective(), DT_MS);
    }
    TEST_ASSERT_TRUE(fsm.isPreheatDone());

    // 3. Start Reflow (transitions to SOAK / REFLOW)
    TEST_ASSERT_TRUE(fsm.startReflow());
    uint8_t stateAfterStart = (uint8_t)fsm.getState();
    TEST_ASSERT_TRUE(stateAfterStart == (uint8_t)fsm::ReflowState::SOAK || stateAfterStart == (uint8_t)fsm::ReflowState::REFLOW);

    // 4. Verify step markers exist
    TEST_ASSERT_TRUE(fsm.getMarkerCount() > 0);
}

// 6. PID Gain Scheduling Interpolation Test
static void test_fsm_pid_library_gain_scheduling()
{
    config::MachineSettings settings;
    settings.settleTimeS       = 1;    // Fast settle for unit test
    settings.holdLowTolerance  = 5.0f; // Wide tolerance – actual temp always in window
    settings.holdHighTolerance = 5.0f;
    settings.pidLibraryEnabled = true; // Enable PID library interpolation
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    topPid.begin();
    botPid.begin();

    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    // Injektion einer PID-Bibliothek mit temperaturabhängigen Stützpunkten
    config::PidLibrary pidLib;
    // Bottom: 100°C -> (1.5, 0.01, 0.4), 180°C -> (3.0, 0.03, 0.8)
    pidLib.bottom.push_back({100.0f, 1.5f, 0.010f, 0.4f});
    pidLib.bottom.push_back({180.0f, 3.0f, 0.030f, 0.8f});
    // Top: 150°C -> (2.0, 0.02, 0.5), 230°C -> (4.0, 0.04, 1.2)
    pidLib.top.push_back({150.0f, 2.0f, 0.020f, 0.5f});
    pidLib.top.push_back({230.0f, 4.0f, 0.040f, 1.2f});

    fsm.setPidLibrary(pidLib);

    // 1. Profil mit Bottom 180°C und Top 230°C
    config::ReflowProfile prof;
    prof.name = "PID Test Profile";
    // Profile constants kept here so the tick budget below stays in sync automatically
    constexpr float    BOT_TARGET_TEMP = 180.0f;
    constexpr float    BOT_ACTUAL_TEMP = 180.0f;  // temp fed into update() – already at target
    constexpr float    BOT_START_TEMP  = 25.0f;   // initial measured temp (used for ramp budget)
    constexpr float    BOT_RAMP_RATE   = 10.0f;   // deg-C/s  (matches step ramp value)
    constexpr uint32_t BOT_STEP_TIME_S = 1;
    prof.stepsBottom.push_back({BOT_TARGET_TEMP, BOT_STEP_TIME_S, BOT_RAMP_RATE});
    prof.stepsTop.push_back({230.0f, 1, 10.0f});

    // Tick budget derived from configured values – stays correct if any constant changes:
    //   ramp   = ceil((target - start) / ramp_rate) seconds worth of ticks
    //   settle = settleTimeS ticks
    //   hold   = max(step_time - settleTimeS, 0) ticks  (+1 for the expiry tick)
    //   +10 safety margin
    constexpr uint32_t RAMP_MS = static_cast<uint32_t>(
                                     (BOT_TARGET_TEMP - BOT_START_TEMP) / BOT_RAMP_RATE * 1000.0f);
    const uint32_t SETTLE_MS   = settings.settleTimeS * 1000UL;
    const uint32_t HOLD_MS     = (BOT_STEP_TIME_S * 1000UL > SETTLE_MS)
                                 ? (BOT_STEP_TIME_S * 1000UL - SETTLE_MS) : 0UL;
    const int preheatTicks     = static_cast<int>((RAMP_MS + SETTLE_MS + HOLD_MS) / DT_MS) + 10;

    // 2. Start Preheat -> Bottom PID muss exakt die Gains für 180°C übernehmen (3.0, 0.03, 0.8)
    TEST_ASSERT_TRUE(fsm.startPreheat(prof));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, botPid.getKp());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.030f, botPid.getKi());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.8f, botPid.getKd());

    // Advance preheat ticks to complete preheat phase
    for (int i = 0; i < preheatTicks && !fsm.isPreheatDone(); ++i) {
        fsm.update(BOT_START_TEMP, BOT_ACTUAL_TEMP, DT_MS);
    }
    TEST_ASSERT_TRUE(fsm.isPreheatDone());

    // 3. Start Reflow -> Top PID muss exakt die Gains für 230°C übernehmen (4.0, 0.04, 1.2)
    TEST_ASSERT_TRUE(fsm.startReflow());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, topPid.getKp());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.040f, topPid.getKi());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.2f, topPid.getKd());

    // 4. Test mit deaktivierter PID-Bibliothek -> Feste Standard-Gains müssen greifen
    settings.pidLibraryEnabled = false;
    settings.topKp = 1.8f;
    settings.topKi = 0.025f;
    settings.topKd = 0.9f;
    fsm::ReflowFSM fsmFixed(settings, outputs, topPid, botPid);
    TEST_ASSERT_TRUE(fsmFixed.startPreheat(prof));
    for (int i = 0; i < preheatTicks && !fsmFixed.isPreheatDone(); ++i) {
        fsmFixed.update(BOT_START_TEMP, BOT_ACTUAL_TEMP, DT_MS);
    }
    TEST_ASSERT_TRUE(fsmFixed.isPreheatDone());
    TEST_ASSERT_TRUE(fsmFixed.startReflow());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.8f, topPid.getKp());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.025f, topPid.getKi());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f, topPid.getKd());
}

// 7. Time Above Liquidus (TAL) Accumulation Test
static void test_fsm_tal_accumulation()
{
    config::MachineSettings settings;
    settings.settleTimeS = 1;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    config::ReflowProfile prof;
    prof.stepsBottom.push_back({150.0f, 1, 10.0f});
    prof.stepsTop.push_back({230.0f, 1, 10.0f});

    TEST_ASSERT_TRUE(fsm.startPreheat(prof));
    for (int i = 0; i < 50 && !fsm.isPreheatDone(); ++i) {
        fsm.update(25.0f, 150.0f, DT_MS);
    }
    TEST_ASSERT_TRUE(fsm.isPreheatDone());

    TEST_ASSERT_TRUE(fsm.startReflow()); // Transitions to SOAK/REFLOW

    // Update with top temp below liquidus (210°C) -> TAL must remain 0
    for (int i = 0; i < 10; ++i) {
        fsm.update(210.0f, 150.0f, DT_MS);
    }
    TEST_ASSERT_EQUAL_UINT32(0, fsm.getTalSec());

    // Update with top temp above liquidus (225°C) for 2000 ms = 2s -> TAL must be 2s
    const int talTicks = static_cast<int>(2000 / DT_MS);
    for (int i = 0; i < talTicks; ++i) {
        fsm.update(225.0f, 150.0f, DT_MS);
    }
    TEST_ASSERT_EQUAL_UINT32(2, fsm.getTalSec());
}

// 8. Fault Inhibit & Safety Reset Test
static void test_fsm_fault_and_reset()
{
    config::MachineSettings settings;
    settings.coolingSafeTemp = 45.0f;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    // 1. Trigger fault when hot (T=150°C > 45°C)
    fsm.triggerFault(150.0f, 150.0f);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::FAULT, (uint8_t)fsm.getState());
    TEST_ASSERT_TRUE(outputs.isInhibited());
    TEST_ASSERT_TRUE(fsm.getFanEffective()); // Fan stays on to cool down
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, fsm.getTopSetpoint());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, fsm.getBottomSetpoint());

    // 2. Cooled down (T=30°C <= 45°C) -> Fan turns off automatically
    fsm.update(30.0f, 30.0f, DT_MS);
    TEST_ASSERT_FALSE(fsm.getFanEffective());

    // 3. Reset fault -> Returns to IDLE and clears inhibit
    fsm.resetFault();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::IDLE, (uint8_t)fsm.getState());
    TEST_ASSERT_FALSE(outputs.isInhibited());
}

// 9. Step Skip Dynamic Transition Test
static void test_fsm_skip_step()
{
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    config::ReflowProfile prof;
    prof.stepsBottom.push_back({100.0f, 30, 1.0f});
    prof.stepsBottom.push_back({150.0f, 30, 1.0f});
    prof.stepsTop.push_back({200.0f, 30, 1.0f});

    TEST_ASSERT_TRUE(fsm.startPreheat(prof));
    TEST_ASSERT_EQUAL_UINT8(0, fsm.getBotStepIndex());

    // Skip first bottom step -> advances to step 1
    fsm.skipStep();
    TEST_ASSERT_EQUAL_UINT8(1, fsm.getBotStepIndex());

    // Skip second bottom step -> preheat finishes
    fsm.skipStep();
    fsm.update(25.0f, 150.0f, DT_MS);
    TEST_ASSERT_TRUE(fsm.isPreheatDone());
}

// 10. Settle-Gate Duration Deduction Test
static void test_fsm_settle_gate_deduction()
{
    constexpr uint32_t STEP_TIME_S  = 10;
    constexpr float    STEP_TEMP    = 100.0f;
    constexpr float    ACTUAL_TEMP  = 100.0f; // always within tolerance window

    config::MachineSettings settings;
    settings.settleTimeS       = 3;
    settings.holdLowTolerance  = 2.0f;
    settings.holdHighTolerance = 2.0f;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    config::ReflowProfile prof;
    // Step time 10s, ramp 0.0f (instant jump to settle)
    prof.stepsBottom.push_back({STEP_TEMP, STEP_TIME_S, 0.0f});

    TEST_ASSERT_TRUE(fsm.startPreheat(prof));

    // First tick: Setpoint jumps to 100°C, enters settling (ramp phase exhausted instantly)
    fsm.update(25.0f, ACTUAL_TEMP, DT_MS);
    TEST_ASSERT_TRUE(fsm.isBottomSettling());
    TEST_ASSERT_FALSE(fsm.isBottomHolding());

    // Settle loop: needs exactly settleTimeS*1000/DT_MS ticks to fill the accumulator.
    // The settle→hold transition fires inside the settle block which then immediately
    // returns false — so holdRemainMs is NOT yet decremented in that same tick.
    // isHolding is therefore already visible after exactly these ticks, no extra tick needed.
    const int settleTicks = static_cast<int>(settings.settleTimeS * 1000UL / DT_MS);
    for (int i = 0; i < settleTicks; ++i) {
        fsm.update(25.0f, ACTUAL_TEMP, DT_MS);
    }

    // After settleTimeS settle, hold must be active.
    // holdRemain = step.time - settleTimeS = 10 - 3 = 7 s
    const uint32_t expectedHold = STEP_TIME_S - settings.settleTimeS;
    TEST_ASSERT_FALSE(fsm.isBottomSettling());
    TEST_ASSERT_TRUE(fsm.isBottomHolding());
    TEST_ASSERT_EQUAL_UINT32(expectedHold, fsm.getBotHoldRemainSec());
}

// 11. Buzzer Notification Pulse Test
static void test_fsm_buzzer_pulse_behavior()
{
    // Use explicit, minimal settle time so the test is fast and self-contained.
    // All loop bounds are derived from these values, so changing them here is
    // sufficient — no magic numbers scattered through the test body.
    config::MachineSettings settings;
    settings.settleTimeS       = 1;    // 1 s settle instead of default 5 s
    settings.holdLowTolerance  = 5.0f; // wide tolerance: actual=100 °C is always in window
    settings.holdHighTolerance = 5.0f;

    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    // Profile: one step at 100 °C, 1 s hold, instant jump (ramp=0)
    config::ReflowProfile prof;
    constexpr uint32_t STEP_TIME_S = 1;
    prof.stepsBottom.push_back({100.0f, STEP_TIME_S, 0.0f});

    TEST_ASSERT_TRUE(fsm.startPreheat(prof));

    // Tick budget derived from settings:
    //   1 tick  – ramp phase (instant jump, transitions to settling, returns false)
    //   ceil(settleTimeS * 1000 / dt) ticks – settle accumulation
    //   1 tick  – hold expires (holdRemain = max(STEP_TIME_S - settleTimeS, 0) = 0 → done in one tick)
    //   + small safety margin
    const int preheatTicks =
        static_cast<int>((settings.settleTimeS * 1000UL + STEP_TIME_S * 1000UL) / DT_MS) + 10;

    for (int i = 0; i < preheatTicks && !fsm.isPreheatDone(); ++i) {
        fsm.update(25.0f, 100.0f, DT_MS);
    }
    TEST_ASSERT_TRUE(fsm.isPreheatDone());
    TEST_ASSERT_TRUE(outputs.getBuzzerState()); // Buzzer must be ON immediately after preheat done

    // Drive the FSM until the buzzer duration expires.
    // Derived from the compile-time constant so the test survives constant changes.
    const int buzzerTicks =
        static_cast<int>(config::Buzzer::PREHEAT_DONE_BEEP_MS / DT_MS) + 2; // +2: boundary safety

    for (int i = 0; i < buzzerTicks; ++i) {
        fsm.update(25.0f, 100.0f, DT_MS);
    }
    TEST_ASSERT_FALSE(outputs.getBuzzerState()); // Buzzer must be OFF after beep duration
}

// 12. Autotune State Machine Transitions Test
static void test_fsm_autotune_state_transition()
{
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    TEST_ASSERT_TRUE(fsm.startAutotune(true, 180.0f));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::AUTOTUNE, (uint8_t)fsm.getState());
    TEST_ASSERT_EQUAL_STRING("AUTOTUNE", fsm.getStateString());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, fsm.getTopSetpoint());

    fsm.stopAutotune();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::COOLING, (uint8_t)fsm.getState());
}

// 13. Backup Maintenance State Lockout Test
static void test_fsm_backup_state_lock()
{
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    TEST_ASSERT_TRUE(fsm.enterBackupState());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::BACKUP, (uint8_t)fsm.getState());
    TEST_ASSERT_EQUAL_STRING("BACKUP", fsm.getStateString());

    // While in BACKUP, starting a profile must be rejected
    config::ReflowProfile prof;
    prof.stepsBottom.push_back({100.0f, 10, 1.0f});
    TEST_ASSERT_FALSE(fsm.startPreheat(prof));

    fsm.exitBackupState();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::IDLE, (uint8_t)fsm.getState());
}

// 14. Invalid / Empty Profile Rejection Test
static void test_fsm_empty_profile_rejection()
{
    config::MachineSettings settings;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    config::ReflowProfile emptyProf;
    TEST_ASSERT_FALSE(fsm.startPreheat(emptyProf));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::IDLE, (uint8_t)fsm.getState());
}

// 15. Emergency Stop Under Active Heating Test
static void test_fsm_emergency_stop_under_power()
{
    config::MachineSettings settings;
    settings.coolingSafeTemp = 45.0f;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    config::ReflowProfile prof;
    prof.stepsBottom.push_back({180.0f, 60, 2.0f});
    prof.stepsTop.push_back({225.0f, 60, 2.0f});

    TEST_ASSERT_TRUE(fsm.startPreheat(prof));
    fsm.update(120.0f, 120.0f, DT_MS);
    TEST_ASSERT_TRUE(fsm.getBottomSetpoint() > 0.0f);

    // E-Stop Trigger
    fsm.stop();

    // Hot system (120°C > 45°C) -> must transition to COOLING, zero setpoints
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::COOLING, (uint8_t)fsm.getState());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, fsm.getTopSetpoint());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, fsm.getBottomSetpoint());

    // Advance 11s past the configured fan delay (10s)
    const int fanDelayTicks = static_cast<int>(11000 / DT_MS);
    for (int i = 0; i < fanDelayTicks; ++i) {
        fsm.update(120.0f, 120.0f, DT_MS);
    }
    TEST_ASSERT_TRUE(fsm.getFanEffective());
}

// 16. Settle-Gate Tolerance Excursion Pause Test
static void test_fsm_tolerance_excursion_pause()
{
    config::MachineSettings settings;
    settings.settleTimeS       = 2;
    settings.holdLowTolerance  = 2.0f; // Window: 98°C to 102°C for target 100°C
    settings.holdHighTolerance = 2.0f;
    output::OutputManager outputs;
    pid::PIDController topPid;
    pid::PIDController botPid;
    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);

    config::ReflowProfile prof;
    prof.stepsBottom.push_back({100.0f, 10, 0.0f}); // Instant ramp step

    TEST_ASSERT_TRUE(fsm.startPreheat(prof));

    // 1 tick to finish ramp
    fsm.update(25.0f, 100.0f, DT_MS);
    TEST_ASSERT_TRUE(fsm.isBottomSettling());

    // Temperature drops below tolerance (95°C < 98°C) -> settle accumulator must stall
    for (int i = 0; i < 30; ++i) {
        fsm.update(25.0f, 95.0f, DT_MS);
    }
    // Must still be settling because 95°C is out of tolerance band!
    TEST_ASSERT_TRUE(fsm.isBottomSettling());
    TEST_ASSERT_FALSE(fsm.isBottomHolding());
}

// 17. End-to-End Multi-Step BGA Reflow Profile Full Fast-Forward Simulation
static void test_fsm_end_to_end_600s_simulation()
{
    config::MachineSettings settings;
    settings.settleTimeS       = 1;
    settings.holdLowTolerance  = 5.0f;
    settings.holdHighTolerance = 5.0f;
    settings.coolingSafeTemp   = 80.0f;
    output::OutputManager outputs;
    pid::PIDController topPid(5.0f, 0.2f, 1.0f);
    pid::PIDController botPid(5.0f, 0.2f, 1.0f);
    topPid.begin();
    botPid.begin();
    topPid.setAutomatic(true);
    botPid.setAutomatic(true);

    fsm::ReflowFSM fsm(settings, outputs, topPid, botPid);
    sim::ThermalSimulator sim;
    sim.reset(25.0f);

    // Standard Lead-Free BGA Multi-Step Profile
    config::ReflowProfile prof;
    prof.name = "SAC305 Deep Simulation";
    prof.stepsBottom.push_back({150.0f, 3, 20.0f}); // Fast Preheat step
    prof.stepsTop.push_back({180.0f, 2, 20.0f});    // Soak step
    prof.stepsTop.push_back({230.0f, 2, 20.0f});    // Peak Reflow step

    // Phase 1: PREHEAT
    TEST_ASSERT_TRUE(fsm.startPreheat(prof));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::PREHEAT, (uint8_t)fsm.getState());

    // Run until preheat completes
    for (int i = 0; i < 2000 && !fsm.isPreheatDone(); ++i) {
        fsm.update(sim.getTopTemperature(), sim.getBottomTemperature(), DT_MS);
        botPid.compute();
        topPid.compute();
        sim.update(topPid.getOutput(), botPid.getOutput(), fsm.getFanEffective(), DT_MS);
    }
    TEST_ASSERT_TRUE(fsm.isPreheatDone());

    // Phase 2: Start Reflow (SOAK & REFLOW)
    TEST_ASSERT_TRUE(fsm.startReflow());
    uint8_t st = (uint8_t)fsm.getState();
    TEST_ASSERT_TRUE(st == (uint8_t)fsm::ReflowState::SOAK || st == (uint8_t)fsm::ReflowState::REFLOW);

    // Run through SOAK, REFLOW, COOLING until DONE
    for (int i = 0; i < 8000 && fsm.getState() != fsm::ReflowState::DONE; ++i) {
        fsm.update(sim.getTopTemperature(), sim.getBottomTemperature(), DT_MS);
        botPid.compute();
        topPid.compute();
        sim.update(topPid.getOutput(), botPid.getOutput(), fsm.getFanEffective(), DT_MS);
    }

    // Must successfully finish in DONE state
    TEST_ASSERT_EQUAL_UINT8((uint8_t)fsm::ReflowState::DONE, (uint8_t)fsm.getState());
    TEST_ASSERT_EQUAL_STRING("DONE", fsm.getStateString());
    TEST_ASSERT_TRUE(fsm.getElapsedSec() > 0);
}

// ============================================================================
// TEST RUNNER ENTRY POINT
// ============================================================================

void run_fsm_tests()
{
    RUN_TEST(test_fsm_initial_state);
    RUN_TEST(test_fsm_start_preheat);
    RUN_TEST(test_fsm_stop);
    RUN_TEST(test_fsm_overrides);
    RUN_TEST(test_fsm_full_profile_simulation);
    RUN_TEST(test_fsm_pid_library_gain_scheduling);
    RUN_TEST(test_fsm_tal_accumulation);
    RUN_TEST(test_fsm_fault_and_reset);
    RUN_TEST(test_fsm_skip_step);
    RUN_TEST(test_fsm_settle_gate_deduction);
    RUN_TEST(test_fsm_buzzer_pulse_behavior);
    RUN_TEST(test_fsm_autotune_state_transition);
    RUN_TEST(test_fsm_backup_state_lock);
    RUN_TEST(test_fsm_empty_profile_rejection);
    RUN_TEST(test_fsm_emergency_stop_under_power);
    RUN_TEST(test_fsm_tolerance_excursion_pause);
    RUN_TEST(test_fsm_end_to_end_600s_simulation);
}
