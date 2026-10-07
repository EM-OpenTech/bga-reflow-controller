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
 * @file reflow_fsm.hpp
 * @brief Dual-channel BGA Reflow Finite State Machine.
 *
 * Coordinates profile execution (Preheat -> Soak -> Reflow -> Cooling -> Done),
 * settle-gate temperature stability windows, TAL accumulation, PID gain scheduling,
 * manual fan/lamp overrides, autotuning, and safety fault states.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include "config/machine_config.hpp"
#include "output/output_manager.hpp"
#include "pid/pid_controller.hpp"
#include "pid/pid_autotuner.hpp"

namespace fsm {

// ============================================================================
// FSM INTERNAL CONSTANTS
// ============================================================================

/// Temperature threshold for automatic SOAK → REFLOW transition (°C)
constexpr float REFLOW_ZONE_MIN_TEMP = 217.0f;

/// Duration to hold DONE state before returning to IDLE (seconds)
constexpr uint32_t DONE_HOLD_SEC = config::Buzzer::REFLOW_DONE_BEEP_MS / 1000UL;

/// Maximum number of step markers stored (aligned with profile step limit)
constexpr size_t MAX_STEP_MARKERS = config::Limits::MAX_PROFILE_STEPS;

// ============================================================================
// REFLOW PROCESS STATES
// ============================================================================

/**
 * @brief Reflow process FSM states.
 *
 * State transitions:
 *   IDLE → PREHEAT       : startPreheat() via UI or physical long-press START
 *   PREHEAT → SOAK       : startReflow() after preheatDone, via UI or physical short-press START
 *   SOAK → REFLOW        : automatic when topTemp >= REFLOW_ZONE_MIN_TEMP (217°C)
 *   REFLOW → COOLING     : all stepsTop[] completed
 *   COOLING → DONE       : fan min duration elapsed AND bottomTemp <= coolingSafeTemp
 *   DONE → IDLE          : automatic after DONE_HOLD_SEC seconds
 *   Any → FAULT          : triggerFault() from SafetyWatchdog or emergency
 *   FAULT → IDLE         : resetFault() via short STOP press
 *   Any active → COOLING : stop() via UI or long STOP press
 *   PREHEAT/SOAK/REFLOW  : short STOP press → skipStep()
 */
enum class ReflowState : uint8_t {
    IDLE     = 0,  ///< Standby – both heaters OFF, fan/lamp manual override always available
    PREHEAT  = 1,  ///< Bottom runs profile steps; Top locked at 0%
    SOAK     = 2,  ///< Top runs profile steps; Bottom holds last preheat temperature
    REFLOW   = 3,  ///< Top at peak (>= 217°C TAL); Bottom holds
    COOLING  = 4,  ///< Both heaters OFF; fan ON after delay; waits for coolingSafeTemp
    DONE     = 5,  ///< Process complete; buzzer; 3s visualisation; → IDLE
    FAULT    = 6,  ///< Emergency: SSR inhibit set; fan stays ON if T > coolingSafeTemp
    AUTOTUNE = 7,  ///< RESERVED – normal process blocked (future implementation)
    BACKUP   = 8   ///< RESERVED – JSON backup up/download (future implementation)
};

// ============================================================================
// STEP MARKER (for chart visualisation)
// ============================================================================

/**
 * @brief Records the moment a Hold phase starts for chart step-marker overlays.
 * Sent to frontend via WebSocket and /api/history endpoint.
 */
struct StepMarker {
    uint32_t timeS      = 0;      ///< Total process elapsed time when Hold started (seconds)
    uint8_t  stepIndex  = 0;      ///< Profile step index (0 = first step)
    float    targetTemp = 0.0f;   ///< Step target temperature (°C)
    bool     isTop      = false;  ///< true = Top channel, false = Bottom channel
};

// ============================================================================
// INTERNAL STEP RUNNER (per channel)
// ============================================================================

/**
 * @brief Tracks execution state of a single profile step for one heater channel.
 *
 * Lifecycle per step:
 *   RAMPING → SETTLING → HOLDING → (next step or finished)
 */
struct StepRunner {
    size_t   stepIndex      = 0;      ///< Current profile step index
    float    currentSet     = 0.0f;   ///< Current interpolated setpoint (°C)
    float    stepStartSet   = 0.0f;   ///< Setpoint at start of this step (for ramping)
    uint32_t settleAccumMs  = 0;      ///< Accumulated ms inside tolerance window
    uint32_t holdRemainMs   = 0;      ///< Remaining hold time after settle (ms)
    bool     isRamping      = false;  ///< Step is in ramp phase
    bool     isSettling     = false;  ///< Step is in settle-gate phase
    bool     isHolding      = false;  ///< Step is in hold phase
    bool     isFinished     = false;  ///< All steps completed
    bool     stepMarkerSent = false;  ///< Prevent duplicate marker on step entry
};

/**
 * @class ReflowFSM
 * @brief Dual-channel BGA reflow process state machine.
 *
 * Orchestrates the dual-channel BGA reflow process:
 *   1. PREHEAT  – Bottom heater runs profile, Top locked
 *   2. SOAK     – Top heater ramps through profile, Bottom holds
 *   3. REFLOW   – Top at peak temperature (>= 217°C, TAL zone)
 *   4. COOLING  – Both heaters OFF, fan manages cool-down
 *   5. DONE     – Process complete, notify user
 *
 * Features:
 *   - Per-step Settle-Gate: temperature must be stable for settleTimeS before Hold timer starts
 *   - Settle time deducted from Hold timer (no artificial step prolongation)
 *   - PID Library Gain Scheduling: gains interpolated at each step transition
 *   - Fan/Lamp: manual override always available regardless of FSM state
 *   - Stop: graceful (→ COOLING) or emergency (→ FAULT + inhibit)
 *   - Short STOP press: skipStep() during active process, resetFault() in FAULT
 *
 * Dependencies (injected via constructor):
 *   - output::OutputManager  – GPIO hardware control
 *   - pid::PIDController     – Top and Bottom PID engines
 *   - config::MachineSettings – runtime parameters
 */
class ReflowFSM {
public:
    /**
     * @brief Construct FSM with all required dependencies.
     *
     * @param settings   Reference to loaded MachineSettings (StorageManager output)
     * @param outputs    Reference to initialized OutputManager
     * @param topPid     Reference to Top channel PIDController
     * @param bottomPid  Reference to Bottom channel PIDController
     */
    ReflowFSM(const config::MachineSettings& settings,
               output::OutputManager&         outputs,
               pid::PIDController&            topPid,
               pid::PIDController&            bottomPid);

    // ========================================================================
    // PROCESS CONTROL
    // ========================================================================

    /**
     * @brief Start preheat process (Bottom heater profile only, Top locked).
     * Trigger: UI "Preheat" button OR physical START long press (>2s).
     * @param profile Loaded reflow profile (from StorageManager)
     * @return true if started successfully (must be in IDLE state)
     */
    bool startPreheat(const config::ReflowProfile& profile);

    /**
     * @brief Activate Top heater after preheat completion (SOAK phase).
     * Trigger: UI "Start" button OR physical START short press (<2s).
     * Only valid when state == PREHEAT and _preheatDone == true.
     * @return true if transition to SOAK was successful
     */
    bool startReflow();

    /**
     * @brief Graceful stop – transitions current process to COOLING.
     * Trigger: UI Stop button OR physical STOP long press (>2s).
     */
    void stop();

    /**
     * @brief Skip current profile step (advance to next step immediately).
     * Trigger: Physical STOP short press (<2s) during PREHEAT / SOAK / REFLOW.
     */
    void skipStep();

    /**
     * @brief Trigger emergency fault state.
     * Called by SafetyWatchdog on any safety violation.
     * Sets OutputManager inhibit, keeps fan ON if temperature > coolingSafeTemp.
     *
     * @param topTemp    Current top sensor reading (°C)
     * @param bottomTemp Current bottom sensor reading (°C)
     */
    void triggerFault(float topTemp, float bottomTemp);

    /**
     * @brief Reset fault and return to IDLE.
     * Trigger: Physical STOP short press (<2s) when state == FAULT.
     * Clears inhibit and resets all internal state.
     */
    void resetFault();

    /**
     * @brief Start PID autotune test on a specific heater channel.
     * @param isTop true for Top heater, false for Bottom heater
     * @param targetTemp Target temperature (°C)
     * @return true if autotune was started, false if state does not permit
     */
    bool startAutotune(bool isTop, float targetTemp);

    /**
     * @brief Stop / abort active autotune test and return to IDLE.
     */
    void stopAutotune();

    /**
     * @brief Enter BACKUP state (locks heating and all start commands).
     * Only permitted from IDLE or DONE.
     * @return true if transitioned to BACKUP, false otherwise
     */
    bool enterBackupState();

    /**
     * @brief Exit BACKUP state and return safely to IDLE.
     */
    void exitBackupState();

    /**
     * @brief Get const reference to internal PidAutotuner instance.
     * @return Const reference to PidAutotuner
     */
    const pid::PidAutotuner& getAutotuner() const { return _autotuner; }

    /**
     * @brief Get mutable reference to internal PidAutotuner instance.
     * @return Reference to PidAutotuner
     */
    pid::PidAutotuner& getAutotuner()             { return _autotuner; }

    /**
     * @brief Set or update active PID library used for gain scheduling.
     * @param lib PidLibrary with configured setpoint gain interpolation points
     */
    void setPidLibrary(const config::PidLibrary& lib) { _pidLibrary = lib; }

    /**
     * @brief Get const reference to active PID library.
     * @return Const reference to PidLibrary
     */
    const config::PidLibrary& getPidLibrary()   const { return _pidLibrary; }

    /**
     * @brief Get mutable reference to active PID library.
     * @return Mutable reference to PidLibrary
     */
    config::PidLibrary& getPidLibrary()               { return _pidLibrary; }

    // ========================================================================
    // MAIN UPDATE LOOP (called at 5 Hz / 200 ms from control_task)
    // ========================================================================

    /**
     * @brief Main FSM tick – called periodically from control_task on Core 1 (dtMs = config::Timing::CONTROL_LOOP_PERIOD_MS).
     *
     * Executes:
     *   - Profile step ramping (setpoint interpolation)
     *   - Settle-Gate evaluation per channel
     *   - Hold timer countdown
     *   - PID setpoint updates
     *   - State transitions
     *   - Cooling fan logic
     *   - DONE → IDLE countdown
     *
     * @param topTemp    Current top thermocouple temperature (°C) – EMA filtered
     * @param bottomTemp Current bottom thermocouple temperature (°C) – EMA filtered
     * @param dtMs       Elapsed time since last call (milliseconds, typically 200ms)
     */
    void update(float topTemp, float bottomTemp, uint32_t dtMs);

    // ========================================================================
    // FAN / LAMP MANUAL OVERRIDE (available in ALL states)
    // ========================================================================

    /**
     * @brief Set fan manual override state.
     * OR'd with FSM automatic fan control. Valid in all states.
     * @param on true = fan forced ON by user, false = release override
     */
    void setFanOverride(bool on);

    /**
     * @brief Set lamp manual override state.
     * Valid in all states regardless of FSM process.
     * @param on true = lamp ON, false = lamp OFF
     */
    void setLampOverride(bool on);

    // ========================================================================
    // STATE GETTERS
    // ========================================================================

    /**
     * @brief Get current FSM state enum.
     * @return ReflowState enum value
     */
    ReflowState getState()         const { return _state; }

    /**
     * @brief Get human-readable string representation of current state.
     * @return String literal (e.g., "IDLE", "PREHEAT", "SOAK", "REFLOW", etc.)
     */
    const char* getStateString()   const;

    /**
     * @brief Check if bottom heater preheat profile has completed.
     * @return true if preheat hold temperature reached and preheat finished
     */
    bool        isPreheatDone()    const { return _preheatDone; }

    /**
     * @brief Get total active process elapsed time in seconds.
     * @return Process elapsed time in seconds
     */
    uint32_t    getElapsedSec()    const { return _totalElapsedMs / 1000UL; }

    /**
     * @brief Get active setpoint for top heater channel (°C).
     * @return Setpoint in °C (0.0f if top channel inactive)
     */
    float getTopSetpoint() const {
        if (_state == ReflowState::SOAK || _state == ReflowState::REFLOW) {
            return _topRunner.currentSet;
        }
        if (_state == ReflowState::AUTOTUNE && _autotuner.isTop()) {
            return _topRunner.currentSet;
        }
        return 0.0f;
    }

    /**
     * @brief Get active setpoint for bottom heater channel (°C).
     * @return Setpoint in °C (0.0f if bottom channel inactive)
     */
    float getBottomSetpoint() const {
        if (_state == ReflowState::PREHEAT) {
            return _preheatDone ? _preheatHoldTemp : _bottomRunner.currentSet;
        }
        if (_state == ReflowState::SOAK || _state == ReflowState::REFLOW) {
            return _preheatHoldTemp;
        }
        if (_state == ReflowState::AUTOTUNE && !_autotuner.isTop()) {
            return _bottomRunner.currentSet;
        }
        return 0.0f;
    }

    /**
     * @brief Check if the FSM is currently executing an active thermal process.
     * @return true during PREHEAT, SOAK, REFLOW, COOLING, or AUTOTUNE
     */
    bool isProcessActiveState() const {
        return (_state == ReflowState::PREHEAT ||
                _state == ReflowState::SOAK    ||
                _state == ReflowState::REFLOW  ||
                _state == ReflowState::COOLING ||
                _state == ReflowState::AUTOTUNE);
    }

    /**
     * @brief Check if top channel is actively waiting in settle-gate tolerance window.
     * @return true if settling and within approach threshold
     */
    bool isTopSettling() const {
        if (!isProcessActiveState()) return false;
        if (_topRunner.isFinished || _topRunner.isHolding) return false;
        if (_profile.stepsTop.empty() || _topRunner.stepIndex >= _profile.stepsTop.size()) return false;
        float target = _profile.stepsTop[_topRunner.stepIndex].temp;
        float approachThresh = target - 5.0f; // Shows settle within 5°C of target
        return (_topRunner.isSettling && _lastTopTemp >= approachThresh);
    }

    /**
     * @brief Check if bottom channel is actively waiting in settle-gate tolerance window.
     * @return true if settling and within approach threshold
     */
    bool isBottomSettling() const {
        if (!isProcessActiveState()) return false;
        if (_bottomRunner.isFinished || _bottomRunner.isHolding) return false;
        if (_profile.stepsBottom.empty() || _bottomRunner.stepIndex >= _profile.stepsBottom.size()) return false;
        float target = _profile.stepsBottom[_bottomRunner.stepIndex].temp;
        float approachThresh = target - 5.0f; // Shows settle within 5°C of target
        return (_bottomRunner.isSettling && _lastBottomTemp >= approachThresh);
    }

    /**
     * @brief Check if top channel is currently in holding phase of a step.
     * @return true if top channel is holding
     */
    bool     isTopHolding()        const {
        if (!isProcessActiveState()) return false;
        return _topRunner.isHolding;
    }

    /**
     * @brief Check if bottom channel is currently in holding phase of a step.
     * @return true if bottom channel is holding
     */
    bool     isBottomHolding()     const {
        if (!isProcessActiveState()) return false;
        return _bottomRunner.isHolding;
    }

    /**
     * @brief Get remaining settle-gate countdown for top channel in seconds.
     * @return Remaining settle time in seconds (rounded up)
     */
    uint32_t getTopSettleRemainSec() const {
        if (_topRunner.isHolding || !isTopSettling()) return 0;
        uint32_t targetMs = _settings.settleTimeS * 1000UL;
        uint32_t remainMs = (targetMs > _topRunner.settleAccumMs) ? (targetMs - _topRunner.settleAccumMs) : 0;
        return (remainMs + 999) / 1000UL;
    }

    /**
     * @brief Get remaining settle-gate countdown for bottom channel in seconds.
     * @return Remaining settle time in seconds (rounded up)
     */
    uint32_t getBotSettleRemainSec() const {
        if (_bottomRunner.isHolding || !isBottomSettling()) return 0;
        uint32_t targetMs = _settings.settleTimeS * 1000UL;
        uint32_t remainMs = (targetMs > _bottomRunner.settleAccumMs) ? (targetMs - _bottomRunner.settleAccumMs) : 0;
        return (remainMs + 999) / 1000UL;
    }

    /**
     * @brief Get remaining hold time for top channel in seconds.
     * @return Remaining hold duration in seconds
     */
    uint32_t getTopHoldRemainSec() const {
        if (!isTopHolding()) return 0;
        return _topRunner.holdRemainMs / 1000UL;
    }

    /**
     * @brief Get remaining hold time for bottom channel in seconds.
     * @return Remaining hold duration in seconds
     */
    uint32_t getBotHoldRemainSec() const {
        if (!isBottomHolding()) return 0;
        return _bottomRunner.holdRemainMs / 1000UL;
    }

    /**
     * @brief Get cumulative Time Above Liquidus (TAL >= 217°C) in seconds.
     * @return Accumulated TAL in seconds
     */
    uint32_t getTalSec()           const { return _talAccumMs / 1000UL; }

    /**
     * @brief Get current step index executed on top heater.
     * @return Step index (0-based)
     */
    uint8_t  getTopStepIndex()     const { return static_cast<uint8_t>(_topRunner.stepIndex); }

    /**
     * @brief Get current step index executed on bottom heater.
     * @return Step index (0-based)
     */
    uint8_t  getBotStepIndex()     const { return static_cast<uint8_t>(_bottomRunner.stepIndex); }

    // Step markers for chart visualisation
    /**
     * @brief Get pointer to step markers array.
     * @return Array pointer to StepMarker
     */
    const StepMarker* getStepMarkers()  const { return _stepMarkers; }

    /**
     * @brief Get total number of recorded step markers.
     * @return Step marker count
     */
    size_t            getMarkerCount()  const { return _markerCount; }

    /**
     * @brief Clear all stored step markers.
     */
    void              clearMarkers()          { _markerCount = 0; }

    /**
     * @brief Check effective status of cooling fan (auto OR manual override).
     * @return true if fan is actively commanded ON
     */
    bool getFanEffective()  const { return _fanAuto || _fanOverride; }

    /**
     * @brief Check effective status of work lamp.
     * @return true if lamp is commanded ON
     */
    bool getLampEffective() const { return _lampOverride; }

    /**
     * @brief Set active profile (e.g. on boot from defaultProfile) without starting process.
     * @param profile Loaded profile configuration
     */
    void setActiveProfile(const config::ReflowProfile& profile) {
        _profile = profile;
    }

    /**
     * @brief Get filename of currently loaded profile.
     * @return Const reference to profile filename string
     */
    const std::string& getActiveProfileFile() const { return _profile.file; }

private:
    // ========================================================================
    // Injected dependencies
    // ========================================================================
    const config::MachineSettings& _settings;  ///< Reference to machine configuration settings
    output::OutputManager&         _outputs;   ///< Reference to hardware output manager
    pid::PIDController&            _topPid;    ///< Reference to top heater PID controller
    pid::PIDController&            _bottomPid; ///< Reference to bottom heater PID controller

    // ========================================================================
    // Active profile
    // ========================================================================
    config::ReflowProfile _profile;            ///< Active reflow profile definition

    // ========================================================================
    // Process state
    // ========================================================================
    ReflowState  _state          = ReflowState::IDLE; ///< Current FSM process state
    bool         _preheatDone    = false;             ///< True if preheat phase completed successfully
    uint32_t     _totalElapsedMs = 0;                 ///< Total elapsed process duration (ms)
    uint32_t     _talAccumMs     = 0;                 ///< Accumulated Time Above Liquidus (ms)

    // ========================================================================
    // Per-channel step runners
    // ========================================================================
    StepRunner   _topRunner;                          ///< Step runner state for top channel
    StepRunner   _bottomRunner;                       ///< Step runner state for bottom channel

    // ========================================================================
    // Step markers (chart overlays)
    // ========================================================================
    StepMarker   _stepMarkers[MAX_STEP_MARKERS];      ///< Array of recorded step transition markers
    size_t       _markerCount = 0;                    ///< Number of recorded step markers

    // ========================================================================
    // Cooling state
    // ========================================================================
    uint32_t     _coolingElapsedMs  = 0;              ///< Total time in COOLING state (ms)
    uint32_t     _fanRunMs          = 0;              ///< Time cooling fan has been actively ON (ms)
    bool         _fanAuto           = false;          ///< Automatic cooling fan request flag

    // ========================================================================
    // Done state & Buzzer
    // ========================================================================
    uint32_t     _doneElapsedMs    = 0;               ///< Elapsed time in DONE hold state (ms)
    uint32_t     _buzzerDurationMs = 0;               ///< Configured buzzer notification duration (ms)

    // ========================================================================
    // Fan/Lamp overrides (always active, all states)
    // ========================================================================
    bool         _fanOverride  = false;               ///< Manual fan override state
    bool         _lampOverride = false;               ///< Manual lamp override state

    // ========================================================================
    // PID Library & Autotuner
    // ========================================================================
    pid::PidAutotuner   _autotuner;                   ///< Integrated sTune autotuner instance
    config::PidLibrary  _pidLibrary;                  ///< Gain scheduling PID library

    // ========================================================================
    // Last preheat hold temperature (Bottom channel)
    // ========================================================================
    float        _preheatHoldTemp = 0.0f;             ///< Target hold temperature for bottom heater during soak (°C)
    float        _lastTopTemp     = 25.0f;            ///< Last sampled top thermocouple reading (°C)
    float        _lastBottomTemp  = 25.0f;            ///< Last sampled bottom thermocouple reading (°C)

    // ========================================================================
    // Internal helpers
    // ========================================================================

    /**
     * @brief Process a single profile step for one heater channel.
     * Handles Ramping → Settling → Holding lifecycle with settle-gate.
     *
     * @param runner     Channel StepRunner (mutable reference)
     * @param steps      Profile steps array for this channel
     * @param stepCount  Number of steps
     * @param actualTemp Current measured temperature (°C)
     * @param dtMs       Delta time (ms)
     * @param isTop      true = Top channel (for marker labelling)
     * @return true if all steps are finished
     */
    bool processStepRunner(StepRunner&                         runner,
                           const config::ProfileStep*          steps,
                           size_t                              stepCount,
                           float                               actualTemp,
                           uint32_t                            dtMs,
                           bool                                isTop);

    /**
     * @brief Advance runner to next step or mark as finished.
     */
    void advanceStep(StepRunner& runner, size_t stepCount);

    /**
     * @brief Record a step marker when Hold phase begins.
     */
    void recordStepMarker(uint8_t stepIndex, float targetTemp, bool isTop);

    /**
     * @brief Apply PID gain scheduling from library at step transition.
     * If pidLibraryEnabled=true, interpolates gains from PidLibrary.
     * Falls back to MachineSettings fixed gains if library is empty or disabled.
     *
     * @param targetTemp Setpoint of the step being entered (°C)
     * @param isTop      true = Top PID, false = Bottom PID
     */
    void applyPidGains(float targetTemp, bool isTop);

    /**
     * @brief Interpolate PID gains linearly between two PidPoint neighbours.
     * Returns the bracketing points for temperature t.
     */
    void interpolatePidGains(float                         t,
                              const config::PidPoint*       pts,
                              size_t                        count,
                              float&                        kp,
                              float&                        ki,
                              float&                        kd) const;

    /**
     * @brief Apply computed fan override OR logic and drive hardware GPIO.
     */
    void applyFanOutput();

    /**
     * @brief Apply lamp override state to hardware GPIO.
     */
    void applyLampOutput();

    /**
     * @brief Initialize a StepRunner for the start of a profile.
     */
    void initStepRunner(StepRunner&             runner,
                         const config::ProfileStep* steps,
                         size_t                  stepCount,
                         float                   startTemp,
                         bool                    isTop);

    /**
     * @brief Enter COOLING state – both heaters OFF, start cooling timer.
     */
    void enterCooling();

    /**
     * @brief Update COOLING logic (fan delay, min duration, temp threshold).
     * @param bottomTemp Current bottom temperature (°C)
     * @param dtMs       Delta time (ms)
     */
    void updateCooling(float bottomTemp, uint32_t dtMs);

    /**
     * @brief Enter DONE state – buzzer, start countdown to IDLE.
     */
    void enterDone();

    /**
     * @brief Reset all runner and timing state to defaults.
     */
    void resetRunners();
};

} // namespace fsm
