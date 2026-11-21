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
 * @file reflow_fsm.cpp
 * @brief Dual-channel BGA Reflow Finite State Machine Implementation.
 *
 * Refactored with high-integrity Dispatcher Pattern:
 * - Decoupled StepRunner for channel trajectory & settle-gate execution
 * - Decoupled CoolingController for fan delays & safe temperature thresholds
 * - Decoupled GainScheduler for linear PID gain interpolation
 * - Robust hardware SSR inhibit synchronization and fail-safe fault handling
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "fsm/reflow_fsm.hpp"
#include "esp_log.h"
#include "pid/gain_scheduler.hpp"
#include <algorithm>
#include <cmath>

namespace {
constexpr const char* TAG = "REFLOW_FSM";
} // namespace

namespace fsm {

// ============================================================================
// Constructor
// ============================================================================

ReflowFSM::ReflowFSM(const config::MachineSettings& settings, output::OutputManager& outputs,
                     pid::PIDController& topPid, pid::PIDController& bottomPid)
    : _settings(settings), _outputs(outputs), _topPid(topPid), _bottomPid(bottomPid), _topRunner(true, settings),
      _bottomRunner(false, settings), _cooling(outputs, settings), _buzzer(outputs) {}

// ============================================================================
// Process Control API
// ============================================================================

bool ReflowFSM::startPreheat(const config::ReflowProfile& profile) {
    if (!canStartPreheat()) {
        ESP_LOGW(TAG, "startPreheat() rejected – not permitted in state: %s", getStateString());
        return false;
    }

    if (_outputs.isInhibited()) {
        ESP_LOGE(TAG, "startPreheat() rejected – hardware SSR output inhibit is active!");
        return false;
    }

    if (profile.stepsBottom.empty() && profile.stepsTop.empty()) {
        ESP_LOGE(TAG, "startPreheat() rejected – both stepsBottom and stepsTop are empty");
        return false;
    }

    _profile = profile;
    _totalElapsedMs = 0;
    _talAccumMs = 0;
    _markerCount = 0;
    _preheatHoldElapsedMs = 0;

    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _topRunner.reset();

    if (profile.stepsBottom.empty()) {
        // Top-only profile: Bottom heater stays off, preheat is immediately ready
        _bottomPid.setSetpoint(0.0f);
        _bottomPid.setAutomatic(false);
        _bottomPid.setManualOutput(0.0f);
        _bottomRunner.reset();
        _preheatDone = true;
        _preheatHoldTemp = 0.0f;
        ESP_LOGI(TAG, "PREHEAT started (Top-only profile '%s') – bottom inactive, ready for reflow",
                 _profile.name.c_str());
    } else {
        _preheatDone = false;
        _bottomPid.setAutomatic(true);
        _bottomRunner.start(_profile.stepsBottom.data(), _profile.stepsBottom.size(), _lastBottomTemp);
        applyPidGains(_bottomRunner.getTargetTemp(), false);
        ESP_LOGI(TAG, "Started PREHEAT with profile '%s' (bottom steps=%zu, top steps=%zu)", _profile.name.c_str(),
                 _profile.stepsBottom.size(), _profile.stepsTop.size());
    }

    _state = ReflowState::PREHEAT;
    return true;
}

bool ReflowFSM::startReflow() {
    if (_state != ReflowState::PREHEAT || !_preheatDone) {
        ESP_LOGW(TAG, "startReflow() rejected – requires PREHEAT with preheatDone=true (current: %s)",
                 getStateString());
        return false;
    }

    if (_outputs.isInhibited()) {
        ESP_LOGE(TAG, "startReflow() rejected – hardware SSR output inhibit is active!");
        return false;
    }

    if (_profile.stepsTop.empty()) {
        // Bottom-only profile: Reflow command finishes preheat and enters cooling
        ESP_LOGI(TAG, "startReflow() – bottom-only profile complete, entering COOLING");
        enterCooling();
        return true;
    }

    _topPid.setAutomatic(true);
    _topRunner.start(_profile.stepsTop.data(), _profile.stepsTop.size(), _lastTopTemp);

    // Initial gain scheduling for top channel step 0
    applyPidGains(_topRunner.getTargetTemp(), true);

    _state = ReflowState::SOAK;
    ESP_LOGI(TAG, "PREHEAT -> SOAK (top heating started with %zu steps)", _profile.stepsTop.size());

    return true;
}

void ReflowFSM::stop() {
    if (_state == ReflowState::IDLE || _state == ReflowState::DONE || _state == ReflowState::FAULT ||
        _state == ReflowState::BACKUP) {
        return;
    }

    ESP_LOGI(TAG, "Stop requested from state %s -> transitioning safely to COOLING", getStateString());
    enterCooling();
}

void ReflowFSM::skipStep() {
    if (!canSkipStep()) {
        return;
    }

    switch (_state) {
    case ReflowState::PREHEAT:
        skipBottomPreheatStep();
        break;

    case ReflowState::SOAK:
    case ReflowState::REFLOW:
        skipTopReflowStep();
        break;

    case ReflowState::COOLING:
        ESP_LOGI(TAG, "skipStep() – skipping COOLING phase directly to DONE");
        _cooling.stopCooling();
        enterDone();
        break;

    default:
        break;
    }
}

void ReflowFSM::skipBottomPreheatStep() {
    StepEvent evt = _bottomRunner.skipStep();
    if (evt == StepEvent::FINISHED) {
        _preheatHoldTemp = _profile.stepsBottom.back().temp;
        _bottomRunner.setCurrentSetpoint(_preheatHoldTemp);
        _bottomPid.setSetpoint(_preheatHoldTemp);

        _buzzer.play(output::BuzzerPattern::PREHEAT_DONE);
        _preheatDone = true;
        _preheatHoldElapsedMs = 0;
        ESP_LOGI(TAG, "PREHEAT skipped to completion -> holding %.1f°C", static_cast<double>(_preheatHoldTemp));
    }
}

void ReflowFSM::skipTopReflowStep() {
    StepEvent evt = _topRunner.skipStep();
    if (evt == StepEvent::FINISHED) {
        ESP_LOGI(TAG, "Top steps skipped to completion -> entering COOLING");
        enterCooling();
    }
}

void ReflowFSM::triggerFault(float topTemp, float bottomTemp, const char* reason) {
    ESP_LOGE(TAG, "FAULT triggered [%s]! (T_top=%.1f°C, T_bot=%.1f°C) -> Hardware SSR Inhibit Set",
             reason ? reason : "UNKNOWN", static_cast<double>(topTemp), static_cast<double>(bottomTemp));

    _state = ReflowState::FAULT;

    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);

    _outputs.setInhibit(true);
    _buzzer.stop();

    // Keep fan ON immediately if temperatures exceed safe limits or are unknown (NaN/Inf sensor fault)
    const bool isHotOrUnknown =
        (std::isnan(topTemp) || std::isinf(topTemp) || std::isnan(bottomTemp) || std::isinf(bottomTemp) ||
         topTemp > _settings.coolingSafeTemp || bottomTemp > _settings.coolingSafeTemp);
    if (isHotOrUnknown) {
        _cooling.startCooling(true);
    } else {
        _cooling.stopCooling();
    }
}

void ReflowFSM::resetFault() {
    if (_state != ReflowState::FAULT) {
        ESP_LOGW(TAG, "resetFault() ignored – not in FAULT state");
        return;
    }

    _outputs.setInhibit(false);
    _buzzer.stop();
    _cooling.stopCooling();
    resetRunners();
    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);
    _preheatDone = false;
    _state = ReflowState::IDLE;

    ESP_LOGI(TAG, "Fault reset confirmed -> returned safely to IDLE and released SSR inhibit");
}

bool ReflowFSM::startAutotune(bool isTop, float targetTemp, uint8_t method) {
    if (!canStartAutotune()) {
        ESP_LOGW(TAG, "startAutotune() rejected – FSM must be in IDLE (current: %s)", getStateString());
        return false;
    }

    if (_outputs.isInhibited()) {
        ESP_LOGE(TAG, "startAutotune() rejected – hardware SSR output inhibit is active!");
        return false;
    }

    _totalElapsedMs = 0;
    _talAccumMs = 0;
    _markerCount = 0;
    _preheatHoldElapsedMs = 0;

    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);

    float startTemp = isTop ? _lastTopTemp : _lastBottomTemp;
    const auto tuningMethod = (method <= static_cast<uint8_t>(sTune::Mixed_PID))
                                  ? static_cast<sTune::TuningMethod>(method)
                                  : sTune::Mixed_PID;
    _autotuner.begin(isTop, targetTemp, startTemp, tuningMethod);

    _state = ReflowState::AUTOTUNE;
    ESP_LOGI(TAG, "Started AUTOTUNE for %s heater at %.1f°C (method=%s [%u])", isTop ? "TOP" : "BOTTOM",
             static_cast<double>(targetTemp), pid::getTuningMethodName(tuningMethod),
             static_cast<unsigned>(tuningMethod));

    return true;
}

void ReflowFSM::stopAutotune() {
    if (_state != ReflowState::AUTOTUNE) {
        return;
    }

    _autotuner.abort();
    _topPid.setManualOutput(0.0f);
    _bottomPid.setManualOutput(0.0f);
    enterCooling();
    ESP_LOGI(TAG, "Autotune stopped by user -> entering COOLING");
}

bool ReflowFSM::enterBackupState() {
    if (!canEnterBackup()) {
        ESP_LOGW(TAG, "enterBackupState() rejected – FSM must be in IDLE or DONE (current: %s)", getStateString());
        return false;
    }

    _state = ReflowState::BACKUP;
    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);
    _cooling.stopCooling();
    ESP_LOGI(TAG, "Entered BACKUP state – heating and start commands locked");
    return true;
}

void ReflowFSM::exitBackupState() {
    if (_state == ReflowState::BACKUP) {
        _state = ReflowState::IDLE;
        ESP_LOGI(TAG, "Exited BACKUP state -> returned to IDLE");
    }
}

// ============================================================================
// Main Update Loop (Dispatcher Pattern)
// ============================================================================

void ReflowFSM::update(float topTemp, float bottomTemp, uint32_t dtMs) {
    _lastTopTemp = topTemp;
    _lastBottomTemp = bottomTemp;

    // Fail-Safe Invariant Guard: If hardware inhibit is set, FSM MUST be in FAULT
    if (_outputs.isInhibited() && _state != ReflowState::FAULT) {
        ESP_LOGE(TAG, "Hardware SSR inhibit detected active during %s -> forcing FAULT", getStateString());
        triggerFault(topTemp, bottomTemp, "HARDWARE_SSR_INHIBIT_ACTIVE");
        return;
    }

    updateTimers(topTemp, dtMs);

    // State Dispatcher
    switch (_state) {
    case ReflowState::IDLE:
        handleStateIdle();
        break;

    case ReflowState::PREHEAT:
        handleStatePreheat(bottomTemp, dtMs);
        break;

    case ReflowState::SOAK:
        handleStateSoak(topTemp, bottomTemp, dtMs);
        break;

    case ReflowState::REFLOW:
        handleStateReflow(topTemp, bottomTemp, dtMs);
        break;

    case ReflowState::COOLING:
        handleStateCooling(topTemp, bottomTemp, dtMs);
        break;

    case ReflowState::DONE:
        handleStateDone(dtMs);
        break;

    case ReflowState::FAULT:
        handleStateFault(topTemp, bottomTemp);
        break;

    case ReflowState::AUTOTUNE:
        handleStateAutotune(topTemp, bottomTemp);
        break;

    case ReflowState::BACKUP:
        // Process blocked during backup
        break;
    }
}

// ============================================================================
// State Handlers
// ============================================================================

void ReflowFSM::updateTimers(float topTemp, uint32_t dtMs) {
    // Process duration timer
    if (isProcessActiveState()) {
        _totalElapsedMs += dtMs;
    }

    // TAL (Time Above Liquidus: >= 217.0°C) accumulation
    bool inTalPhase = (_state == ReflowState::SOAK || _state == ReflowState::REFLOW || _state == ReflowState::COOLING);
    if (inTalPhase && topTemp >= REFLOW_ZONE_MIN_TEMP && topTemp <= config::Limits::MAX_TEMPERATURE) {
        _talAccumMs += dtMs;
    }
}

void ReflowFSM::handleStateIdle() {
    // Standby: outputs driven exclusively by manual overrides, ensure PIDs remain inactive
    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);
}

void ReflowFSM::handleStatePreheat(float bottomTemp, uint32_t dtMs) {
    _topPid.setManualOutput(0.0f);
    _bottomPid.setInput(bottomTemp);

    if (_preheatDone) {
        _bottomPid.setSetpoint(_preheatHoldTemp);

        if (_settings.preheatHoldTimeoutMin > 0) {
            _preheatHoldElapsedMs += dtMs;
            const uint32_t timeoutMs = _settings.preheatHoldTimeoutMin * 60UL * 1000UL;
            if (_preheatHoldElapsedMs >= timeoutMs) {
                ESP_LOGW(TAG, "PREHEAT hold timeout reached (%lu min) -> transitioning safely to COOLING",
                         static_cast<unsigned long>(_settings.preheatHoldTimeoutMin));
                _buzzer.play(output::BuzzerPattern::TIMEOUT_ALERT);
                enterCooling();
            }
        }
        return;
    }

    _bottomPid.setSetpoint(_bottomRunner.getCurrentSetpoint());
    StepEvent evt = _bottomRunner.update(bottomTemp, dtMs);

    if (evt == StepEvent::ENTERED_HOLD) {
        recordStepMarker(static_cast<uint8_t>(_bottomRunner.getStepIndex()), _bottomRunner.getTargetTemp(), false);
        applyPidGains(_bottomRunner.getTargetTemp(), false);
    } else if (evt == StepEvent::FINISHED) {
        _preheatHoldTemp = _profile.stepsBottom.back().temp;
        _bottomRunner.setCurrentSetpoint(_preheatHoldTemp);
        _bottomPid.setSetpoint(_preheatHoldTemp);

        _buzzer.play(output::BuzzerPattern::PREHEAT_DONE);
        _preheatDone = true;
        _preheatHoldElapsedMs = 0;

        ESP_LOGI(TAG, "PREHEAT done – holding %.1f°C, waiting for user start", static_cast<double>(_preheatHoldTemp));
    }
}

void ReflowFSM::handleStateSoak(float topTemp, float bottomTemp, uint32_t dtMs) {
    _bottomPid.setSetpoint(_preheatHoldTemp);
    _bottomPid.setInput(bottomTemp);

    _topPid.setSetpoint(_topRunner.getCurrentSetpoint());
    _topPid.setInput(topTemp);

    StepEvent evt = _topRunner.update(topTemp, dtMs);

    if (evt == StepEvent::ENTERED_HOLD) {
        recordStepMarker(static_cast<uint8_t>(_topRunner.getStepIndex()), _topRunner.getTargetTemp(), true);
        applyPidGains(_topRunner.getTargetTemp(), true);
    }

    if (topTemp >= REFLOW_ZONE_MIN_TEMP && topTemp <= config::Limits::MAX_TEMPERATURE) {
        _state = ReflowState::REFLOW;
        ESP_LOGI(TAG, "SOAK -> REFLOW (topTemp=%.1f°C >= %.1f°C)", static_cast<double>(topTemp),
                 static_cast<double>(REFLOW_ZONE_MIN_TEMP));
    }

    if (evt == StepEvent::FINISHED) {
        enterCooling();
    }
}

void ReflowFSM::handleStateReflow(float topTemp, float bottomTemp, uint32_t dtMs) {
    _bottomPid.setSetpoint(_preheatHoldTemp);
    _bottomPid.setInput(bottomTemp);

    _topPid.setSetpoint(_topRunner.getCurrentSetpoint());
    _topPid.setInput(topTemp);

    StepEvent evt = _topRunner.update(topTemp, dtMs);

    if (evt == StepEvent::ENTERED_HOLD) {
        recordStepMarker(static_cast<uint8_t>(_topRunner.getStepIndex()), _topRunner.getTargetTemp(), true);
        applyPidGains(_topRunner.getTargetTemp(), true);
    }

    if (evt == StepEvent::FINISHED) {
        ESP_LOGI(TAG, "REFLOW complete – all top steps done, entering COOLING");
        enterCooling();
    }
}

void ReflowFSM::handleStateCooling(float topTemp, float bottomTemp, uint32_t dtMs) {
    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);

    if (_cooling.update(topTemp, bottomTemp, dtMs)) {
        enterDone();
    }
}

void ReflowFSM::handleStateDone(uint32_t dtMs) {
    _doneElapsedMs += dtMs;
    if (_doneElapsedMs < DONE_HOLD_SEC * 1000UL) {
        return;
    }

    _buzzer.stop();
    _cooling.stopCooling();
    resetRunners();
    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);
    _preheatDone = false;
    _state = ReflowState::IDLE;
    ESP_LOGI(TAG, "DONE -> IDLE");
}

void ReflowFSM::handleStateFault(float topTemp, float bottomTemp) {
    _cooling.handleFault(topTemp, bottomTemp);
}

void ReflowFSM::handleStateAutotune(float topTemp, float bottomTemp) {
    float outPower = 0.0f;
    float currTemp = _autotuner.isTop() ? topTemp : bottomTemp;
    bool running = _autotuner.step(currTemp, outPower);

    _topPid.setAutomatic(false);
    _topPid.setManualOutput(_autotuner.isTop() ? outPower : 0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(_autotuner.isTop() ? 0.0f : outPower);

    // Guard: Continue stepping while autotune is actively oscillating
    if (running) {
        return;
    }

    if (_autotuner.isFinished()) {
        saveAutotuneGains();
    }

    enterCooling();
    ESP_LOGI(TAG, "AUTOTUNE ended – entered COOLING");
}

void ReflowFSM::saveAutotuneGains() {
    _autotuner.applyResults(_autotuner.isTop() ? _topPid : _bottomPid);

    float tunedKp = 0.0f;
    float tunedKi = 0.0f;
    float tunedKd = 0.0f;
    _autotuner.getResults(tunedKp, tunedKi, tunedKd);
    float tunedTemp = _autotuner.getTargetTemp();

    auto& pts = _autotuner.isTop() ? _pidLibrary.top : _pidLibrary.bottom;

    // Check if an existing point matches the tuned temperature
    auto it = std::find_if(pts.begin(), pts.end(),
                           [tunedTemp](const config::PidPoint& pt) { return std::fabs(pt.temp - tunedTemp) < 1.0f; });
    if (it != pts.end()) {
        it->kp = tunedKp;
        it->ki = tunedKi;
        it->kd = tunedKd;
        return;
    }

    // Add new point if capacity permits
    if (pts.size() >= config::Limits::MAX_PID_POINTS) {
        ESP_LOGW(TAG, "Autotune complete: PID library at capacity (%zu points), point omitted",
                 config::Limits::MAX_PID_POINTS);
        return;
    }

    pts.push_back({tunedTemp, tunedKp, tunedKi, tunedKd});
    std::ranges::sort(pts, [](const config::PidPoint& a, const config::PidPoint& b) { return a.temp < b.temp; });
}

// ============================================================================
// Internal Helpers
// ============================================================================

void ReflowFSM::enterCooling() {
    _state = ReflowState::COOLING;
    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);
    _cooling.startCooling();
}

void ReflowFSM::enterDone() {
    _state = ReflowState::DONE;
    _doneElapsedMs = 0;

    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);

    _buzzer.play(output::BuzzerPattern::REFLOW_DONE);

    ESP_LOGI(TAG, "Reflow process COMPLETE – entered DONE (holding %lu s)", static_cast<unsigned long>(DONE_HOLD_SEC));
}

void ReflowFSM::resetRunners() {
    _topRunner.reset();
    _bottomRunner.reset();
    _preheatHoldTemp = 0.0f;
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ReflowFSM::recordStepMarker(uint8_t stepIndex, float targetTemp, bool isTop) {
    if (_markerCount >= MAX_STEP_MARKERS) {
        ESP_LOGW(TAG, "Step marker capacity reached (%zu), marker omitted", MAX_STEP_MARKERS);
        return;
    }

    _stepMarkers.at(_markerCount++) =
        StepMarker{.timeS = getElapsedSec(), .stepIndex = stepIndex, .targetTemp = targetTemp, .isTop = isTop};

    ESP_LOGI(TAG, "Step marker recorded: %s step %u at %lus -> %.1f°C", isTop ? "Top" : "Bottom", stepIndex,
             static_cast<unsigned long>(getElapsedSec()), static_cast<double>(targetTemp));
}

void ReflowFSM::applyPidGains(float targetTemp, bool isTop) {
    pid::PIDController& pid = isTop ? _topPid : _bottomPid;

    pid::PidGains fallback{.kp = isTop ? _settings.topKp : _settings.bottomKp,
                           .ki = isTop ? _settings.topKi : _settings.bottomKi,
                           .kd = isTop ? _settings.topKd : _settings.bottomKd};

    pid::PidGains gains = fallback;

    if (_settings.pidLibraryEnabled) {
        const auto& pts = isTop ? _pidLibrary.top : _pidLibrary.bottom;
        gains = pid::GainScheduler::interpolate(targetTemp, pts.data(), pts.size(), fallback);
    }

    pid.setTunings(gains.kp, gains.ki, gains.kd);

    ESP_LOGI(TAG, "%s PID gain scheduling at %.0f°C: Kp=%.2f Ki=%.3f Kd=%.2f", isTop ? "Top" : "Bot",
             static_cast<double>(targetTemp), static_cast<double>(gains.kp), static_cast<double>(gains.ki),
             static_cast<double>(gains.kd));
}

// ============================================================================
// Getters
// ============================================================================

const char* ReflowFSM::getStateString() const {
    switch (_state) {
    case ReflowState::IDLE:
        return "IDLE";
    case ReflowState::PREHEAT:
        return _preheatDone ? "PREHEAT_DONE" : "PREHEAT";
    case ReflowState::SOAK:
        return "SOAK";
    case ReflowState::REFLOW:
        return "REFLOW";
    case ReflowState::COOLING:
        return "COOLING";
    case ReflowState::DONE:
        return "DONE";
    case ReflowState::FAULT:
        return "FAULT";
    case ReflowState::AUTOTUNE:
        return "AUTOTUNE";
    case ReflowState::BACKUP:
        return "BACKUP";
    default:
        return "UNKNOWN";
    }
}

float ReflowFSM::getTopSetpoint() const {
    if (_state == ReflowState::SOAK || _state == ReflowState::REFLOW) {
        return _topRunner.getCurrentSetpoint();
    }
    if (_state == ReflowState::AUTOTUNE && _autotuner.isTop()) {
        return _autotuner.getTargetTemp();
    }
    return 0.0f;
}

float ReflowFSM::getBottomSetpoint() const {
    if (_state == ReflowState::PREHEAT) {
        return _preheatDone ? _preheatHoldTemp : _bottomRunner.getCurrentSetpoint();
    }
    if (_state == ReflowState::SOAK || _state == ReflowState::REFLOW) {
        return _preheatHoldTemp;
    }
    if (_state == ReflowState::AUTOTUNE && !_autotuner.isTop()) {
        return _autotuner.getTargetTemp();
    }
    return 0.0f;
}

} // namespace fsm
