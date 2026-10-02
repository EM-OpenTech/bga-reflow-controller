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
 * @brief Implementation of dual-channel BGA Reflow Finite State Machine.
 *
 * Implements state sequencing (IDLE -> PREHEAT -> SOAK -> REFLOW -> COOLING -> DONE -> IDLE),
 * settle-gate stability evaluation, TAL calculation, PID gain interpolation,
 * and fan/lamp override control.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "fsm/reflow_fsm.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cmath>
#include <cstring>

static const char* TAG = "ReflowFSM";

namespace fsm {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ReflowFSM::ReflowFSM(const config::MachineSettings& settings,
                       output::OutputManager&         outputs,
                       pid::PIDController&            topPid,
                       pid::PIDController&            bottomPid)
    : _settings(settings)
    , _outputs(outputs)
    , _topPid(topPid)
    , _bottomPid(bottomPid)
{
    resetRunners();
}

// ============================================================================
// PROCESS CONTROL – PUBLIC API
// ============================================================================

bool ReflowFSM::startPreheat(const config::ReflowProfile& profile)
{
    // Allow starting from IDLE, DONE, or from COOLING if minimum fan cooling duration has elapsed
    bool allowed = false;
    if (_state == ReflowState::IDLE || _state == ReflowState::DONE) {
        allowed = true;
    } else if (_state == ReflowState::COOLING) {
        bool minDurationOk = (_fanRunMs >= static_cast<uint32_t>(_settings.fanCoolingDurationS) * 1000UL);
        if (minDurationOk) {
            allowed = true;
            ESP_LOGI(TAG, "startPreheat() allowed from COOLING (min fan duration %lus elapsed)",
                     (unsigned long)_settings.fanCoolingDurationS);
        } else {
            uint32_t remainS = (_settings.fanCoolingDurationS * 1000UL - _fanRunMs) / 1000UL;
            ESP_LOGW(TAG, "startPreheat() rejected in COOLING – fan must run at least %lu more seconds",
                     (unsigned long)remainS);
        }
    }

    if (!allowed) {
        ESP_LOGW(TAG, "startPreheat() ignored – not permitted in state: %s", getStateString());
        return false;
    }
    if (profile.stepsBottom.empty() && profile.stepsTop.empty()) {
        ESP_LOGE(TAG, "startPreheat() failed – both stepsBottom and stepsTop are empty");
        return false;
    }
    if (profile.stepsBottom.size() > config::Limits::MAX_PROFILE_STEPS || profile.stepsTop.size() > config::Limits::MAX_PROFILE_STEPS) {
        ESP_LOGE(TAG, "startPreheat() failed – step count exceeds MAX_PROFILE_STEPS (%zu vs max %zu)",
                 std::max(profile.stepsBottom.size(), profile.stepsTop.size()), config::Limits::MAX_PROFILE_STEPS);
        return false;
    }

    // If cooling fan was running automatically, turn it off immediately
    _fanAuto = false;
    applyFanOutput();

    _profile = profile;
    resetRunners();
    _totalElapsedMs = 0;
    _talAccumMs     = 0;
    _markerCount    = 0;

    // Lock Top heater at 0%
    _topPid.setSetpoint(0.0f);
    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);

    if (profile.stepsBottom.empty()) {
        // Top-only profile: Bottom heater stays off, preheat is immediately ready
        _bottomPid.setSetpoint(0.0f);
        _bottomPid.setAutomatic(false);
        _bottomPid.setManualOutput(0.0f);
        _preheatDone = true;
        _preheatHoldTemp = 0.0f;
        ESP_LOGI(TAG, "PREHEAT started (Top-only profile) – bottom inactive, ready for reflow");
    } else {
        _preheatDone = false;
        // Initialise Bottom runner from current bottom temperature
        // (setpoint starts at current temp to avoid large initial ramp jump)
        initStepRunner(_bottomRunner,
                       profile.stepsBottom.data(),
                       profile.stepsBottom.size(),
                       0.0f,    // Will be overwritten in first update()
                       false);

        // Apply initial PID gains for first bottom step
        applyPidGains(profile.stepsBottom[0].temp, false);
        _bottomPid.setAutomatic(true);
        ESP_LOGI(TAG, "PREHEAT started – profile: %s, %zu bottom steps",
                 profile.name.c_str(), profile.stepsBottom.size());
    }

    _state = ReflowState::PREHEAT;
    return true;
}

bool ReflowFSM::startReflow()
{
    if (_state != ReflowState::PREHEAT || !_preheatDone) {
        ESP_LOGW(TAG, "startReflow() ignored – preheat not done (state=%s, done=%d)",
                 getStateString(), _preheatDone);
        return false;
    }
    if (_profile.stepsTop.empty()) {
        // Bottom-only profile: Reflow command finishes preheat and enters cooling
        ESP_LOGI(TAG, "startReflow() – bottom-only profile complete, entering COOLING");
        enterCooling();
        return true;
    }

    // Initialise Top runner
    initStepRunner(_topRunner,
                   _profile.stepsTop.data(),
                   _profile.stepsTop.size(),
                   0.0f,
                   true);

    // Apply initial PID gains for first top step
    applyPidGains(_profile.stepsTop[0].temp, true);
    _topPid.setAutomatic(true);

    _state = ReflowState::SOAK;
    ESP_LOGI(TAG, "SOAK started – %zu top steps, bottom holding %.1f°C",
             _profile.stepsTop.size(), _preheatHoldTemp);
    return true;
}

void ReflowFSM::stop()
{
    if (_state == ReflowState::IDLE || _state == ReflowState::COOLING ||
        _state == ReflowState::DONE || _state == ReflowState::FAULT) {
        return;
    }
    ESP_LOGI(TAG, "Graceful stop from state: %s", getStateString());
    enterCooling();
}

void ReflowFSM::skipStep()
{
    if (_state == ReflowState::PREHEAT) {
        advanceStep(_bottomRunner, _profile.stepsBottom.size());
        ESP_LOGI(TAG, "Skip: Bottom step → %zu", _bottomRunner.stepIndex);
    } else if (_state == ReflowState::SOAK || _state == ReflowState::REFLOW) {
        advanceStep(_topRunner, _profile.stepsTop.size());
        ESP_LOGI(TAG, "Skip: Top step → %zu", _topRunner.stepIndex);
    }
}

void ReflowFSM::triggerFault(float topTemp, float bottomTemp)
{
    _outputs.setInhibit(true);   // Immediately cut all SSR outputs
    _outputs.setBuzzer(false);
    _buzzerDurationMs = 0;

    // Keep fan ON if either sensor is still above safe temperature
    bool tooHot = (topTemp    > _settings.coolingSafeTemp ||
                   bottomTemp > _settings.coolingSafeTemp);
    if (tooHot) {
        _fanAuto = true;
        applyFanOutput();
        ESP_LOGW(TAG, "FAULT – fan stays ON (top=%.1f°C bot=%.1f°C > safe=%.1f°C)",
                 topTemp, bottomTemp, _settings.coolingSafeTemp);
    } else {
        _fanAuto = false;
        applyFanOutput();
    }

    // Disable both PIDs
    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);

    // Clear setpoints and runner flags so the UI shows 0°C and hides alerts
    _topRunner.currentSet       = 0.0f;
    _bottomRunner.currentSet    = 0.0f;
    _topRunner.isSettling       = false;
    _topRunner.isHolding        = false;
    _bottomRunner.isSettling    = false;
    _bottomRunner.isHolding     = false;

    _state = ReflowState::FAULT;
    ESP_LOGE(TAG, "FAULT triggered – top=%.1f°C bot=%.1f°C", topTemp, bottomTemp);
}

void ReflowFSM::resetFault()
{
    if (_state != ReflowState::FAULT) {
        ESP_LOGW(TAG, "resetFault() ignored – not in FAULT state");
        return;
    }

    _outputs.setInhibit(false);
    _outputs.setBuzzer(false);
    _buzzerDurationMs = 0;
    _fanAuto = false;
    applyFanOutput();

    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);

    resetRunners();
    _preheatDone = false;
    _state = ReflowState::IDLE;
    ESP_LOGI(TAG, "Fault cleared – returning to IDLE");
}

bool ReflowFSM::startAutotune(bool isTop, float targetTemp)
{
    if (_state != ReflowState::IDLE && _state != ReflowState::DONE) {
        ESP_LOGW(TAG, "startAutotune() rejected – not permitted in state: %s", getStateString());
        return false;
    }

    _outputs.setBuzzer(false);
    _buzzerDurationMs = 0;
    _fanAuto = false;
    applyFanOutput();

    // Disable automatic PID calculation during open-loop test
    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);

    _topRunner.currentSet    = isTop ? targetTemp : 0.0f;
    _bottomRunner.currentSet = !isTop ? targetTemp : 0.0f;

    _totalElapsedMs = 0;
    _talAccumMs     = 0;
    _markerCount    = 0;

    _state = ReflowState::AUTOTUNE;
    _autotuner.begin(isTop, targetTemp, isTop ? _lastTopTemp : _lastBottomTemp);

    ESP_LOGI(TAG, "FSM: Entered AUTOTUNE state for %s heater at %.1f°C",
             isTop ? "TOP" : "BOTTOM", targetTemp);
    return true;
}

void ReflowFSM::stopAutotune()
{
    if (_state != ReflowState::AUTOTUNE) return;

    _autotuner.abort();
    enterCooling();
    ESP_LOGI(TAG, "FSM: AUTOTUNE stopped – entered COOLING");
}

bool ReflowFSM::enterBackupState()
{
    if (_state != ReflowState::IDLE && _state != ReflowState::DONE) {
        ESP_LOGW(TAG, "enterBackupState() rejected – current state is %s", getStateString());
        return false;
    }

    _outputs.setBuzzer(false);
    _buzzerDurationMs = 0;

    // Force all heater outputs to 0% immediately
    _topPid.setSetpoint(0.0f);
    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);

    _bottomPid.setSetpoint(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);

    _topRunner.currentSet    = 0.0f;
    _bottomRunner.currentSet = 0.0f;

    _state = ReflowState::BACKUP;
    ESP_LOGI(TAG, "FSM: Entered BACKUP state (Process locked for configuration/backup management)");
    return true;
}

void ReflowFSM::exitBackupState()
{
    if (_state != ReflowState::BACKUP) return;

    _state = ReflowState::IDLE;
    ESP_LOGI(TAG, "FSM: Exited BACKUP state -> returned to IDLE");
}

// ============================================================================
// FAN / LAMP MANUAL OVERRIDE (all states)
// ============================================================================

void ReflowFSM::setFanOverride(bool on)
{
    _fanOverride = on;
    applyFanOutput();
}

void ReflowFSM::setLampOverride(bool on)
{
    _lampOverride = on;
    applyLampOutput();
}

// ============================================================================
// MAIN UPDATE LOOP (10 Hz, called from control_task)
// ============================================================================

void ReflowFSM::update(float topTemp, float bottomTemp, uint32_t dtMs)
{
    _lastTopTemp    = topTemp;
    _lastBottomTemp = bottomTemp;

    // ------------------------------------------------------------------------
    // Buzzer pulse countdown
    // ------------------------------------------------------------------------
    if (_buzzerDurationMs > 0) {
        if (_buzzerDurationMs <= dtMs) {
            _buzzerDurationMs = 0;
            _outputs.setBuzzer(false);
        } else {
            _buzzerDurationMs -= dtMs;
        }
    }

    // ------------------------------------------------------------------------
    // Accumulate process runtime (only during active process)
    // ------------------------------------------------------------------------
    // In DONE, FAULT, IDLE: _totalElapsedMs retains the duration of the last run
    // until reset in startPreheat() / startAutotune() when a new cycle starts.
    if (_state == ReflowState::PREHEAT || _state == ReflowState::SOAK ||
        _state == ReflowState::REFLOW  || _state == ReflowState::COOLING ||
        _state == ReflowState::AUTOTUNE) {
        _totalElapsedMs += dtMs;
    }

    // ------------------------------------------------------------------------
    // Accumulate TAL (Time Above Liquidus: >= 217.0°C)
    // ------------------------------------------------------------------------
    // Retains final TAL duration in DONE / IDLE until startPreheat().
    if (_state == ReflowState::SOAK || _state == ReflowState::REFLOW || _state == ReflowState::COOLING) {
        if (topTemp >= REFLOW_ZONE_MIN_TEMP) {
            _talAccumMs += dtMs;
        }
    }

    switch (_state) {

    // ------------------------------------------------------------------------
    // IDLE
    // ------------------------------------------------------------------------
    case ReflowState::IDLE:
        // Nothing to do – fan/lamp driven exclusively by overrides
        break;

    // ------------------------------------------------------------------------
    // PREHEAT
    // ------------------------------------------------------------------------
    case ReflowState::PREHEAT: {
        // Top heater stays at 0% – enforced every tick
        _topPid.setManualOutput(0.0f);

        // Initialise bottom setpoint on first tick (start from actual temp)
        if (_bottomRunner.stepIndex == 0 && _bottomRunner.isRamping &&
            _bottomRunner.stepStartSet == 0.0f) {
            _bottomRunner.stepStartSet = bottomTemp;
            _bottomRunner.currentSet   = bottomTemp;
        }

        // Update bottom PID setpoint
        _bottomPid.setSetpoint(_bottomRunner.currentSet);
        _bottomPid.setInput(bottomTemp);

        // Process bottom profile steps
        bool bottomDone = processStepRunner(_bottomRunner,
                                            _profile.stepsBottom.data(),
                                            _profile.stepsBottom.size(),
                                            bottomTemp,
                                            dtMs,
                                            false);

        if (bottomDone && !_preheatDone) {
            // Hold last step temperature indefinitely
            _preheatHoldTemp = _profile.stepsBottom.back().temp;
            _bottomRunner.currentSet = _preheatHoldTemp;
            _bottomPid.setSetpoint(_preheatHoldTemp);

            // Notify user – buzzer short beep
            _outputs.setBuzzer(true);
            _buzzerDurationMs = config::Buzzer::PREHEAT_DONE_BEEP_MS;

            _preheatDone = true;
            ESP_LOGI(TAG, "PREHEAT done – holding %.1f°C, waiting for user start", _preheatHoldTemp);
        }

        // If preheat done, keep bottom at hold temperature
        if (_preheatDone) {
            _bottomRunner.currentSet = _preheatHoldTemp;
            _bottomPid.setSetpoint(_preheatHoldTemp);
        }
        break;
    }

    // ------------------------------------------------------------------------
    // SOAK
    // ------------------------------------------------------------------------
    case ReflowState::SOAK: {
        // Bottom holds preheat temperature
        _bottomPid.setSetpoint(_preheatHoldTemp);
        _bottomPid.setInput(bottomTemp);

        // Initialise top setpoint on first tick
        if (_topRunner.stepIndex == 0 && _topRunner.isRamping &&
            _topRunner.stepStartSet == 0.0f) {
            _topRunner.stepStartSet = topTemp;
            _topRunner.currentSet   = topTemp;
        }

        // Update top PID setpoint
        _topPid.setSetpoint(_topRunner.currentSet);
        _topPid.setInput(topTemp);

        // Process top profile steps
        bool topDone = processStepRunner(_topRunner,
                                          _profile.stepsTop.data(),
                                          _profile.stepsTop.size(),
                                          topTemp,
                                          dtMs,
                                          true);

        // Automatic SOAK → REFLOW when top temp reaches reflow zone
        if (topTemp >= REFLOW_ZONE_MIN_TEMP) {
            _state = ReflowState::REFLOW;
            ESP_LOGI(TAG, "SOAK → REFLOW (topTemp=%.1f°C >= %.1f°C)",
                     topTemp, REFLOW_ZONE_MIN_TEMP);
        }

        if (topDone) {
            enterCooling();
        }
        break;
    }

    // ------------------------------------------------------------------------
    // REFLOW
    // ------------------------------------------------------------------------
    case ReflowState::REFLOW: {
        // Bottom continues holding preheat temperature
        _bottomPid.setSetpoint(_preheatHoldTemp);
        _bottomPid.setInput(bottomTemp);

        // Update top PID
        _topPid.setSetpoint(_topRunner.currentSet);
        _topPid.setInput(topTemp);

        // Process remaining top profile steps
        bool topDone = processStepRunner(_topRunner,
                                          _profile.stepsTop.data(),
                                          _profile.stepsTop.size(),
                                          topTemp,
                                          dtMs,
                                          true);

        if (topDone) {
            ESP_LOGI(TAG, "REFLOW complete – all top steps done, entering COOLING");
            enterCooling();
        }
        break;
    }

    // ------------------------------------------------------------------------
    // COOLING
    // ------------------------------------------------------------------------
    case ReflowState::COOLING:
        updateCooling(bottomTemp, dtMs);
        break;

    // ------------------------------------------------------------------------
    // DONE
    // ------------------------------------------------------------------------
    case ReflowState::DONE:
        _doneElapsedMs += dtMs;
        if (_doneElapsedMs >= DONE_HOLD_SEC * 1000UL) {
            _outputs.setBuzzer(false);
            _fanAuto = false;
            applyFanOutput();
            resetRunners();
            _preheatDone = false;
            _state = ReflowState::IDLE;
            ESP_LOGI(TAG, "DONE → IDLE");
        }
        break;

    // ------------------------------------------------------------------------
    // FAULT
    // ------------------------------------------------------------------------
    case ReflowState::FAULT:
        // In FAULT: if temperature drops below safe, turn fan off
        if (_fanAuto) {
            if (topTemp    <= _settings.coolingSafeTemp &&
                bottomTemp <= _settings.coolingSafeTemp) {
                _fanAuto = false;
                applyFanOutput();
                ESP_LOGI(TAG, "FAULT – temperatures safe, fan OFF");
            }
        }
        break;

    // ------------------------------------------------------------------------
    // AUTOTUNE
    // ------------------------------------------------------------------------
    case ReflowState::AUTOTUNE: {
        float outPower = 0.0f;
        float currTemp = _autotuner.isTop() ? topTemp : bottomTemp;
        bool running   = _autotuner.step(currTemp, outPower);

        if (_autotuner.isTop()) {
            _topPid.setAutomatic(false);
            _topPid.setManualOutput(outPower);
            _bottomPid.setAutomatic(false);
            _bottomPid.setManualOutput(0.0f);
        } else {
            _topPid.setAutomatic(false);
            _topPid.setManualOutput(0.0f);
            _bottomPid.setAutomatic(false);
            _bottomPid.setManualOutput(outPower);
        }

        if (!running) {
            // Autotune completed or aborted
            if (_autotuner.isFinished()) {
                _autotuner.applyResults(_autotuner.isTop() ? _topPid : _bottomPid);

                // Add or update point in _pidLibrary
                float tunedKp, tunedKi, tunedKd;
                _autotuner.getResults(tunedKp, tunedKi, tunedKd);
                float tunedTemp = _autotuner.getTargetTemp();

                auto& pts = _autotuner.isTop() ? _pidLibrary.top : _pidLibrary.bottom;
                bool updated = false;
                for (auto& pt : pts) {
                    if (std::fabs(pt.temp - tunedTemp) < 1.0f) {
                        pt.kp = tunedKp;
                        pt.ki = tunedKi;
                        pt.kd = tunedKd;
                        updated = true;
                        break;
                    }
                }
                if (!updated) {
                    if (pts.size() < config::Limits::MAX_PID_POINTS) {
                        pts.push_back({ tunedTemp, tunedKp, tunedKi, tunedKd });
                        std::sort(pts.begin(), pts.end(), [](const config::PidPoint& a, const config::PidPoint& b) {
                            return a.temp < b.temp;
                        });
                    } else {
                        ESP_LOGW(TAG, "Autotune complete: PID library already at max capacity (%zu points), new point not added",
                                 config::Limits::MAX_PID_POINTS);
                    }
                }
            }

            enterCooling();
            ESP_LOGI(TAG, "AUTOTUNE ended – entered COOLING");
        }
        break;
    }

    // ------------------------------------------------------------------------
    // RESERVED
    // ------------------------------------------------------------------------
    case ReflowState::BACKUP:
        // Process blocked during backup
        break;
    }
}

// ============================================================================
// STATE STRING HELPER
// ============================================================================

const char* ReflowFSM::getStateString() const
{
    switch (_state) {
        case ReflowState::IDLE:     return "IDLE";
        case ReflowState::PREHEAT:  return _preheatDone ? "PREHEAT_DONE" : "PREHEAT";
        case ReflowState::SOAK:     return "SOAK";
        case ReflowState::REFLOW:   return "REFLOW";
        case ReflowState::COOLING:  return "COOLING";
        case ReflowState::DONE:     return "DONE";
        case ReflowState::FAULT:    return "FAULT";
        case ReflowState::AUTOTUNE: return "AUTOTUNE";
        case ReflowState::BACKUP:   return "BACKUP";
        default:                    return "UNKNOWN";
    }
}

// ============================================================================
// INTERNAL: STEP RUNNER PROCESSING
// ============================================================================

bool ReflowFSM::processStepRunner(StepRunner&                runner,
                                    const config::ProfileStep* steps,
                                    size_t                     stepCount,
                                    float                      actualTemp,
                                    uint32_t                   dtMs,
                                    bool                       isTop)
{
    if (runner.isFinished || stepCount == 0) {
        return true;
    }

    // Safety fallback against invalid / disconnected sensor readings
    if (std::isnan(actualTemp) || std::isinf(actualTemp)) {
        return false;
    }

    const config::ProfileStep& step = steps[runner.stepIndex];

    // ------------------------------------------------------------------------
    // Phase 1: RAMPING
    // ------------------------------------------------------------------------
    if (runner.isRamping) {
        // Handle step jump (ramp <= 0) or standard upward ramp
        if (step.ramp <= 0.01f) {
            // Immediate setpoint jump (no ramp requested)
            runner.currentSet  = step.temp;
            runner.isRamping   = false;
            runner.isSettling  = true;
            runner.settleAccumMs = 0;
            ESP_LOGD(TAG, "%s step %zu instant setpoint jump → %.1f°C",
                     isTop ? "Top" : "Bot", runner.stepIndex, step.temp);
        } else if (runner.stepStartSet <= step.temp) {
            // Standard upward ramp
            float rampDeltaPerMs = step.ramp / 1000.0f;  // °C/ms
            runner.currentSet += rampDeltaPerMs * static_cast<float>(dtMs);

            if (runner.currentSet >= step.temp) {
                runner.currentSet    = step.temp;
                runner.isRamping     = false;
                runner.isSettling    = true;
                runner.settleAccumMs = 0;
                ESP_LOGD(TAG, "%s step %zu ramp done → %.1f°C, entering settle",
                         isTop ? "Top" : "Bot", runner.stepIndex, step.temp);
            }
        } else {
            // Downward step (e.g. passive cooling ramp)
            float rampDeltaPerMs = step.ramp / 1000.0f;  // °C/ms
            runner.currentSet -= rampDeltaPerMs * static_cast<float>(dtMs);

            if (runner.currentSet <= step.temp) {
                runner.currentSet    = step.temp;
                runner.isRamping     = false;
                runner.isSettling    = true;
                runner.settleAccumMs = 0;
                ESP_LOGD(TAG, "%s step %zu downward ramp done → %.1f°C, entering settle",
                         isTop ? "Top" : "Bot", runner.stepIndex, step.temp);
            }
        }
        return false;
    }

    // ------------------------------------------------------------------------
    // Phase 2: SETTLING (Settle-Gate)
    // ------------------------------------------------------------------------
    if (runner.isSettling) {
        float low  = step.temp - _settings.holdLowTolerance;
        float high = step.temp + _settings.holdHighTolerance;

        if (actualTemp >= low && actualTemp <= high) {
            runner.settleAccumMs += dtMs;

            if (runner.settleAccumMs >= _settings.settleTimeS * 1000UL) {
                // Settle complete – deduct settle time from hold duration
                uint32_t settleS   = _settings.settleTimeS;
                uint32_t holdS     = (step.time > settleS) ? (step.time - settleS) : 0UL;
                runner.holdRemainMs = holdS * 1000UL;

                runner.isSettling = false;
                runner.isHolding  = true;

                // Record step marker for chart
                if (!runner.stepMarkerSent) {
                    recordStepMarker(static_cast<uint8_t>(runner.stepIndex),
                                     step.temp, isTop);
                    runner.stepMarkerSent = true;
                }

                // Apply PID gain scheduling for this step (at hold start)
                applyPidGains(step.temp, isTop);

                ESP_LOGI(TAG, "%s step %zu settled at %.1f°C, hold=%lus",
                         isTop ? "Top" : "Bot",
                         runner.stepIndex, step.temp, holdS);
            }
        } else {
            // Temperature left tolerance window – reset settle timer
            if (runner.settleAccumMs > 0) {
                ESP_LOGD(TAG, "%s step %zu settle reset (T=%.1f outside [%.1f,%.1f])",
                         isTop ? "Top" : "Bot",
                         runner.stepIndex, actualTemp, low, high);
            }
            runner.settleAccumMs = 0;
        }
        return false;
    }

    // ------------------------------------------------------------------------
    // Phase 3: HOLDING
    // ------------------------------------------------------------------------
    if (runner.isHolding) {
        if (runner.holdRemainMs <= dtMs) {
            runner.holdRemainMs = 0;
            // Advance to next step
            advanceStep(runner, stepCount);
            return runner.isFinished;
        } else {
            runner.holdRemainMs -= dtMs;
        }
    }

    return false;
}

void ReflowFSM::advanceStep(StepRunner& runner, size_t stepCount)
{
    runner.stepIndex++;
    if (runner.stepIndex >= stepCount) {
        runner.isFinished = true;
        runner.isHolding  = false;
        ESP_LOGI(TAG, "All %zu steps completed", stepCount);
        return;
    }

    // Reset for next step
    runner.stepStartSet   = runner.currentSet;
    runner.settleAccumMs  = 0;
    runner.holdRemainMs   = 0;
    runner.isRamping      = true;
    runner.isSettling     = false;
    runner.isHolding      = false;
    runner.stepMarkerSent = false;

    ESP_LOGD(TAG, "Advancing to step %zu", runner.stepIndex);
}

void ReflowFSM::recordStepMarker(uint8_t stepIndex, float targetTemp, bool isTop)
{
    if (_markerCount >= MAX_STEP_MARKERS) {
        ESP_LOGW(TAG, "Step marker buffer full (%zu)", MAX_STEP_MARKERS);
        return;
    }

    StepMarker& m = _stepMarkers[_markerCount++];
    m.timeS      = _totalElapsedMs / 1000UL;
    m.stepIndex  = stepIndex;
    m.targetTemp = targetTemp;
    m.isTop      = isTop;

    ESP_LOGD(TAG, "Marker: %s step %u @ %lus (%.1f°C)",
             isTop ? "Top" : "Bot", stepIndex, m.timeS, targetTemp);
}

// ============================================================================
// INTERNAL: PID GAIN SCHEDULING
// ============================================================================

void ReflowFSM::applyPidGains(float targetTemp, bool isTop)
{
    pid::PIDController& pid = isTop ? _topPid : _bottomPid;

    if (!_settings.pidLibraryEnabled) {
        // Use fixed gains from MachineSettings
        float kp = isTop ? _settings.topKp : _settings.bottomKp;
        float ki = isTop ? _settings.topKi : _settings.bottomKi;
        float kd = isTop ? _settings.topKd : _settings.bottomKd;
        if (kp <= 0.0f || ki < 0.0f || kd < 0.0f) {
            kp = 2.0f;
            ki = isTop ? 0.05f : 0.04f;
            kd = 1.0f;
            ESP_LOGW(TAG, "%s fixed PID gains invalid -> fallback to defaults (Kp=%.2f Ki=%.3f Kd=%.2f)",
                     isTop ? "Top" : "Bot", kp, ki, kd);
        }
        pid.setTunings(kp, ki, kd);
        ESP_LOGD(TAG, "%s PID fixed gains: Kp=%.2f Ki=%.3f Kd=%.2f",
                 isTop ? "Top" : "Bot", kp, ki, kd);
        return;
    }

    // Use PID library interpolation
    const auto& pts = isTop ? _pidLibrary.top : _pidLibrary.bottom;
    float kp = isTop ? _settings.topKp : _settings.bottomKp;
    float ki = isTop ? _settings.topKi : _settings.bottomKi;
    float kd = isTop ? _settings.topKd : _settings.bottomKd;

    if (!pts.empty()) {
        interpolatePidGains(targetTemp, pts.data(), pts.size(), kp, ki, kd);
    }

    // Safety guard: If interpolated gains have invalid values (Kp <= 0, Ki < 0, or Kd < 0), fallback to MachineSettings
    if (kp <= 0.0f || ki < 0.0f || kd < 0.0f) {
        float fallbackKp = isTop ? _settings.topKp : _settings.bottomKp;
        float fallbackKi = isTop ? _settings.topKi : _settings.bottomKi;
        float fallbackKd = isTop ? _settings.topKd : _settings.bottomKd;
        ESP_LOGW(TAG, "%s PID gains at %.0f°C invalid (Kp=%.2f Ki=%.3f Kd=%.2f) -> fallback to MachineSettings (Kp=%.2f Ki=%.3f Kd=%.2f)",
                 isTop ? "Top" : "Bot", targetTemp, kp, ki, kd, fallbackKp, fallbackKi, fallbackKd);
        kp = fallbackKp;
        ki = fallbackKi;
        kd = fallbackKd;
    }

    pid.setTunings(kp, ki, kd);
    ESP_LOGI(TAG, "%s PID gain scheduling at %.0f°C: Kp=%.2f Ki=%.3f Kd=%.2f (points=%zu)",
             isTop ? "Top" : "Bot", targetTemp, kp, ki, kd, pts.size());
}

void ReflowFSM::interpolatePidGains(float                   t,
                                      const config::PidPoint* pts,
                                      size_t                  count,
                                      float&                  kp,
                                      float&                  ki,
                                      float&                  kd) const
{
    if (count == 0) return;

    // Below first point – use first point
    if (t <= pts[0].temp) {
        kp = pts[0].kp; ki = pts[0].ki; kd = pts[0].kd;
        return;
    }
    // Above last point – use last point
    if (t >= pts[count - 1].temp) {
        kp = pts[count-1].kp; ki = pts[count-1].ki; kd = pts[count-1].kd;
        return;
    }

    // Find bracketing pair and interpolate linearly
    for (size_t i = 0; i < count - 1; ++i) {
        if (t >= pts[i].temp && t < pts[i+1].temp) {
            float ratio = (t - pts[i].temp) / (pts[i+1].temp - pts[i].temp);
            kp = pts[i].kp + ratio * (pts[i+1].kp - pts[i].kp);
            ki = pts[i].ki + ratio * (pts[i+1].ki - pts[i].ki);
            kd = pts[i].kd + ratio * (pts[i+1].kd - pts[i].kd);
            return;
        }
    }
}

// ============================================================================
// INTERNAL: COOLING STATE
// ============================================================================

void ReflowFSM::enterCooling()
{
    // Both heaters OFF immediately
    _topPid.setAutomatic(false);
    _topPid.setManualOutput(0.0f);
    _bottomPid.setAutomatic(false);
    _bottomPid.setManualOutput(0.0f);

    _outputs.setBuzzer(false);
    _buzzerDurationMs = 0;

    // Clear setpoints so the UI shows 0°C (heaters are off)
    _topRunner.currentSet       = 0.0f;
    _bottomRunner.currentSet    = 0.0f;

    _topRunner.isSettling       = false;
    _topRunner.isHolding        = false;
    _topRunner.settleAccumMs    = 0;
    _topRunner.holdRemainMs     = 0;

    _bottomRunner.isSettling    = false;
    _bottomRunner.isHolding     = false;
    _bottomRunner.settleAccumMs = 0;
    _bottomRunner.holdRemainMs  = 0;

    _coolingElapsedMs = 0;
    _fanRunMs         = 0;
    _fanAuto          = false;
    applyFanOutput();

    _state = ReflowState::COOLING;
    ESP_LOGI(TAG, "COOLING – heaters OFF, fan delay=%lus",
             (unsigned long)_settings.fanCoolingDelayS);
}

void ReflowFSM::updateCooling(float bottomTemp, uint32_t dtMs)
{
    _coolingElapsedMs += dtMs;

    // Fan start delay
    if (!_fanAuto &&
        _coolingElapsedMs >= static_cast<uint32_t>(_settings.fanCoolingDelayS) * 1000UL)
    {
        _fanAuto  = true;
        _fanRunMs = 0;
        applyFanOutput();
        ESP_LOGI(TAG, "COOLING – fan ON (delay elapsed)");
    }

    // Fan run timer
    if (_fanAuto) {
        _fanRunMs += dtMs;
    }

    // Transition to DONE when minimum fan duration elapsed AND temperature safe
    bool minDurationOk = (_fanRunMs >= static_cast<uint32_t>(_settings.fanCoolingDurationS) * 1000UL);
    bool tempOk        = (bottomTemp <= _settings.coolingSafeTemp);

    if (minDurationOk && tempOk) {
        _fanAuto = false;
        applyFanOutput();
        ESP_LOGI(TAG, "COOLING done – %.1f°C <= %.1f°C, fan OFF → DONE",
                 bottomTemp, _settings.coolingSafeTemp);
        enterDone();
    }
}

void ReflowFSM::enterDone()
{
    _doneElapsedMs = 0;
    _outputs.setBuzzer(true);   // Audible completion notification
    _buzzerDurationMs = config::Buzzer::REFLOW_DONE_BEEP_MS;
    _state = ReflowState::DONE;
    ESP_LOGI(TAG, "DONE – buzzer ON, returning to IDLE in %lus", (unsigned long)DONE_HOLD_SEC);
}

// ============================================================================
// INTERNAL: HELPER UTILITIES
// ============================================================================

void ReflowFSM::initStepRunner(StepRunner&                runner,
                                 const config::ProfileStep* steps,
                                 size_t                     stepCount,
                                 float                      startTemp,
                                 bool                       isTop)
{
    (void)isTop;
    (void)steps;
    (void)stepCount;

    runner.stepIndex      = 0;
    runner.currentSet     = startTemp;
    runner.stepStartSet   = startTemp;
    runner.settleAccumMs  = 0;
    runner.holdRemainMs   = 0;
    runner.isRamping      = true;
    runner.isSettling     = false;
    runner.isHolding      = false;
    runner.isFinished     = false;
    runner.stepMarkerSent = false;
}

void ReflowFSM::resetRunners()
{
    initStepRunner(_topRunner,    nullptr, 0, 0.0f, true);
    initStepRunner(_bottomRunner, nullptr, 0, 0.0f, false);
    _preheatHoldTemp  = 0.0f;
    _coolingElapsedMs = 0;
    _fanRunMs         = 0;
    _doneElapsedMs    = 0;
    _fanAuto          = false;
    _buzzerDurationMs = 0;
    _outputs.setBuzzer(false);
    // Note: _totalElapsedMs, _talAccumMs and _markerCount are preserved
    // so the completed run curve & metrics remain inspectable in IDLE until next startPreheat().
}

void ReflowFSM::applyFanOutput()
{
    // OR logic: FSM auto OR user override
    _outputs.setFan(_fanAuto || _fanOverride);
}

void ReflowFSM::applyLampOutput()
{
    _outputs.setLamp(_lampOverride);
}

} // namespace fsm
