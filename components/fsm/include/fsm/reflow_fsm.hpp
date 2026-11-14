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
 * High-integrity state machine coordinating profile execution (Preheat -> Soak
 * -> Reflow -> Cooling -> Done), safety interlocks, settle-gate stability windows,
 * and TAL accumulation.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "config/machine_config.hpp"
#include "fsm/cooling_controller.hpp"
#include "fsm/step_runner.hpp"
#include "output/output_manager.hpp"
#include "pid/pid_autotuner.hpp"
#include "pid/pid_controller.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace fsm {

// ============================================================================
// FSM CONSTANTS
// ============================================================================

/// Temperature threshold for automatic SOAK → REFLOW transition (°C)
inline constexpr float REFLOW_ZONE_MIN_TEMP = 217.0f;

/// Duration to hold DONE state before returning to IDLE (seconds)
inline constexpr uint32_t DONE_HOLD_SEC = config::Buzzer::REFLOW_DONE_BEEP_MS / 1000UL;

/// Maximum number of step markers stored (covers up to MAX_PROFILE_STEPS for both Top and Bottom)
inline constexpr size_t MAX_STEP_MARKERS = config::Limits::MAX_PROFILE_STEPS * 2;

// ============================================================================
// REFLOW PROCESS STATES
// ============================================================================

/**
 * @brief Reflow process FSM states.
 */
enum class ReflowState : uint8_t {
    IDLE = 0,     ///< Standby – heaters OFF, fan/lamp manual overrides available
    PREHEAT = 1,  ///< Bottom runs profile steps; Top locked at 0%
    SOAK = 2,     ///< Top runs profile steps; Bottom holds last preheat temperature
    REFLOW = 3,   ///< Top at peak (>= 217°C TAL zone); Bottom holds
    COOLING = 4,  ///< Both heaters OFF; fan manages cooldown to coolingSafeTemp
    DONE = 5,     ///< Process complete; buzzer notification; hold -> IDLE
    FAULT = 6,    ///< Emergency fault: SSR inhibit active; fan runs if T > coolingSafeTemp
    AUTOTUNE = 7, ///< PID Autotune mode (sTune execution)
    BACKUP = 8    ///< Maintenance/Backup state
};

// ============================================================================
// STEP MARKER (for frontend telemetry and charting)
// ============================================================================

/**
 * @brief Records the moment a step enters its Hold phase for chart overlays.
 */
struct StepMarker {
    uint32_t timeS = 0;      ///< Total process elapsed time when Hold started (seconds)
    uint8_t stepIndex = 0;   ///< Profile step index (0 = first step)
    float targetTemp = 0.0f; ///< Step target temperature (°C)
    bool isTop = false;      ///< true = Top channel, false = Bottom channel
};

/**
 * @class ReflowFSM
 * @brief Master Finite State Machine for BGA Reflow Controller.
 */
class ReflowFSM {
public:
    /**
     * @brief Construct FSM with all required dependencies.
     */
    ReflowFSM(const config::MachineSettings& settings, output::OutputManager& outputs, pid::PIDController& topPid,
              pid::PIDController& bottomPid);

    // ========================================================================
    // Process Control API
    // ========================================================================

    /**
     * @brief Start preheat process (Bottom heater profile only, Top locked).
     */
    bool startPreheat(const config::ReflowProfile& profile);

    /**
     * @brief Activate Top heater after preheat completion (SOAK phase).
     */
    bool startReflow();

    /**
     * @brief Graceful stop – transitions current process safely to COOLING.
     */
    void stop();

    /**
     * @brief Skip current profile step (advance to next step immediately).
     */
    void skipStep();

    /**
     * @brief Trigger emergency fault state.
     * @param topTemp Current measured top temperature (°C)
     * @param bottomTemp Current measured bottom temperature (°C)
     * @param reason Diagnostic reason for triggering fault state (optional)
     */
    void triggerFault(float topTemp, float bottomTemp, const char* reason = "EMERGENCY_FAULT");

    /**
     * @brief Reset fault and return safely to IDLE.
     * Guaranteed fail-safe: Rejects reset if hardware SSR inhibit is still active.
     */
    void resetFault();

    /**
     * @brief Start PID autotune test on a specific heater channel with selected tuning rule.
     */
    bool startAutotune(bool isTop, float targetTemp, uint8_t method = 4);

    /**
     * @brief Stop / abort active autotune test and return to IDLE.
     */
    void stopAutotune();

    /**
     * @brief Enter BACKUP state (locks heating and all start commands).
     */
    bool enterBackupState();

    /**
     * @brief Exit BACKUP state and return safely to IDLE.
     */
    void exitBackupState();

    // ========================================================================
    // Main Update Loop (5 Hz / 200 ms)
    // ========================================================================

    /**
     * @brief Main periodic FSM update loop.
     * @param topTemp Current measured top temperature (°C)
     * @param bottomTemp Current measured bottom temperature (°C)
     * @param dtMs Elapsed delta time in milliseconds
     */
    void update(float topTemp, float bottomTemp, uint32_t dtMs);

    // ========================================================================
    // Fan / Lamp Manual Overrides
    // ========================================================================

    void setFanOverride(bool on) { _cooling.setFanOverride(on); }
    void setLampOverride(bool on) { _cooling.setLampOverride(on); }
    [[nodiscard]] bool getFanEffective() const { return _cooling.isFanEffective(); }
    [[nodiscard]] bool getLampEffective() const { return _cooling.isLampEffective(); }

    // ========================================================================
    // State & Telemetry Getters
    // ========================================================================

    [[nodiscard]] ReflowState getState() const { return _state; }
    [[nodiscard]] const char* getStateString() const;
    [[nodiscard]] bool isPreheatDone() const { return _preheatDone; }
    [[nodiscard]] uint32_t getElapsedSec() const { return _totalElapsedMs / 1000UL; }
    [[nodiscard]] uint32_t getTalSec() const { return _talAccumMs / 1000UL; }

    [[nodiscard]] float getTopSetpoint() const;
    [[nodiscard]] float getBottomSetpoint() const;

    [[nodiscard]] bool isProcessActiveState() const;

    [[nodiscard]] bool isTopSettling() const { return isProcessActiveState() && _topRunner.isSettling(_lastTopTemp); }
    [[nodiscard]] bool isBottomSettling() const {
        return isProcessActiveState() && _bottomRunner.isSettling(_lastBottomTemp);
    }

    [[nodiscard]] bool isTopHolding() const { return isProcessActiveState() && _topRunner.isHolding(); }
    [[nodiscard]] bool isBottomHolding() const { return isProcessActiveState() && _bottomRunner.isHolding(); }

    [[nodiscard]] uint32_t getTopSettleRemainSec() const {
        return isProcessActiveState() ? _topRunner.getSettleRemainSec(_lastTopTemp) : 0;
    }
    [[nodiscard]] uint32_t getBotSettleRemainSec() const {
        return isProcessActiveState() ? _bottomRunner.getSettleRemainSec(_lastBottomTemp) : 0;
    }

    [[nodiscard]] uint32_t getTopHoldRemainSec() const {
        return isProcessActiveState() ? _topRunner.getHoldRemainSec() : 0;
    }
    [[nodiscard]] uint32_t getBotHoldRemainSec() const {
        return isProcessActiveState() ? _bottomRunner.getHoldRemainSec() : 0;
    }

    [[nodiscard]] uint8_t getTopStepIndex() const { return static_cast<uint8_t>(_topRunner.getStepIndex()); }
    [[nodiscard]] uint8_t getBotStepIndex() const { return static_cast<uint8_t>(_bottomRunner.getStepIndex()); }

    [[nodiscard]] const StepMarker* getStepMarkers() const { return _stepMarkers.data(); }
    [[nodiscard]] size_t getMarkerCount() const { return _markerCount; }
    void clearMarkers() { _markerCount = 0; }

    // ========================================================================
    // Profile & PID Library Management
    // ========================================================================

    void setActiveProfile(const config::ReflowProfile& profile) { _profile = profile; }
    [[nodiscard]] const std::string& getActiveProfileFile() const { return _profile.file; }

    void setPidLibrary(const config::PidLibrary& lib) {
        _pidLibrary = lib;
        std::ranges::sort(_pidLibrary.top,
                          [](const config::PidPoint& a, const config::PidPoint& b) { return a.temp < b.temp; });
        std::ranges::sort(_pidLibrary.bottom,
                          [](const config::PidPoint& a, const config::PidPoint& b) { return a.temp < b.temp; });
    }
    [[nodiscard]] const config::PidLibrary& getPidLibrary() const { return _pidLibrary; }
    config::PidLibrary& getPidLibrary() { return _pidLibrary; }

    [[nodiscard]] const pid::PidAutotuner& getAutotuner() const { return _autotuner; }
    pid::PidAutotuner& getAutotuner() { return _autotuner; }

    /**
     * @brief Persist autotune results into PidLibrary if finished.
     */
    void saveAutotuneGains();

private:
    const config::MachineSettings& _settings;
    output::OutputManager& _outputs;
    pid::PIDController& _topPid;
    pid::PIDController& _bottomPid;

    config::ReflowProfile _profile;
    ReflowState _state = ReflowState::IDLE;
    bool _preheatDone = false;
    uint32_t _totalElapsedMs = 0;
    uint32_t _talAccumMs = 0;

    StepRunner _topRunner;
    StepRunner _bottomRunner;
    CoolingController _cooling;

    std::array<StepMarker, MAX_STEP_MARKERS> _stepMarkers{};
    size_t _markerCount = 0;

    uint32_t _doneElapsedMs = 0;
    uint32_t _buzzerDurationMs = 0;

    pid::PidAutotuner _autotuner;
    config::PidLibrary _pidLibrary;

    float _preheatHoldTemp = 0.0f;
    float _lastTopTemp = 25.0f;
    float _lastBottomTemp = 25.0f;

    // ========================================================================
    // Clean Dispatcher State Handlers (Dispatcher Pattern)
    // ========================================================================

    void updateBuzzer(uint32_t dtMs);
    void updateTimers(float topTemp, uint32_t dtMs);
    void handleStateIdle();
    void handleStatePreheat(float bottomTemp, uint32_t dtMs);
    void handleStateSoak(float topTemp, float bottomTemp, uint32_t dtMs);
    void handleStateReflow(float topTemp, float bottomTemp, uint32_t dtMs);
    void handleStateCooling(float topTemp, float bottomTemp, uint32_t dtMs);
    void handleStateDone(uint32_t dtMs);
    void handleStateFault(float topTemp, float bottomTemp);
    void handleStateAutotune(float topTemp, float bottomTemp);

    void enterCooling();
    void enterDone();
    void resetRunners();
    void recordStepMarker(uint8_t stepIndex, float targetTemp, bool isTop);
    void applyPidGains(float targetTemp, bool isTop);
};

} // namespace fsm
