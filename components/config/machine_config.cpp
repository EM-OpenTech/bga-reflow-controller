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
 * @file machine_config.cpp
 * @brief Implementation of domain model and machine configuration validation.
 *
 * Implements declarative, tabular boundary validation for ProfileStep, ReflowProfile,
 * PidPoint, PidLibrary, and MachineSettings using linear guard clauses and decoupled
 * validation primitives.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "config/machine_config.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace config {

namespace {

// ============================================================================
// Internal Validation Primitives (Tabular Range Checkers & String Validators)
// ============================================================================

struct FloatRangeRule {
    float val = 0.0f;
    float min = 0.0f;
    float max = 0.0f;
    const char* errorMsg = nullptr;
};

struct UintRangeRule {
    uint32_t val = 0;
    uint32_t min = 0;
    uint32_t max = 0;
    const char* errorMsg = nullptr;
};

bool checkFloatRules(std::span<const FloatRangeRule> rules, ValidationResult& out) {
    auto it = std::ranges::find_if(
        rules, [](const auto& rule) { return !std::isfinite(rule.val) || rule.val < rule.min || rule.val > rule.max; });
    if (it != rules.end()) {
        out = ValidationResult{.valid = false, .errorMessage = it->errorMsg};
        return false;
    }
    return true;
}

bool checkUintRules(std::span<const UintRangeRule> rules, ValidationResult& out) {
    auto it = std::ranges::find_if(rules, [](const auto& rule) { return rule.val < rule.min || rule.val > rule.max; });
    if (it != rules.end()) {
        out = ValidationResult{.valid = false, .errorMessage = it->errorMsg};
        return false;
    }
    return true;
}

/**
 * @brief Validates a filename against path traversal attempts and maximum character length.
 */
bool checkFilename(const std::string& filename, const char* traversalErrorMsg, const char* lengthErrorMsg,
                   ValidationResult& out) {
    if (filename.empty()) {
        return true;
    }
    if (filename.find("..") != std::string::npos || filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos) {
        out = {.valid = false, .errorMessage = traversalErrorMsg};
        return false;
    }
    if (filename.length() > Limits::MAX_FILENAME_LENGTH) {
        out = {.valid = false, .errorMessage = lengthErrorMsg};
        return false;
    }
    return true;
}

/**
 * @brief Validates all items in a vector using their intrinsic validate() method.
 */
template <typename T> bool validateElements(const std::vector<T>& elements, ValidationResult& out) {
    for (const auto& item : elements) {
        auto res = item.validate();
        if (!res.valid) {
            out = res;
            return false;
        }
    }
    return true;
}

/**
 * @brief Checks if a vector of PidPoints has strictly increasing temperature setpoints.
 */
bool isStrictlyIncreasing(const std::vector<PidPoint>& points, const char* errorMsg, ValidationResult& out) {
    for (size_t i = 1; i < points.size(); ++i) {
        if (points[i].temp <= points[i - 1].temp) {
            out = {.valid = false, .errorMessage = errorMsg};
            return false;
        }
    }
    return true;
}

} // anonymous namespace

// ============================================================================
// ProfileStep Validation
// ============================================================================

ValidationResult ProfileStep::validate() const {
    const std::array floatRules = {FloatRangeRule{.val = temp,
                                                  .min = Limits::MIN_TEMPERATURE,
                                                  .max = Limits::MAX_TEMPERATURE,
                                                  .errorMsg = "Step target temperature out of range (20..300°C)"},
                                   FloatRangeRule{.val = ramp,
                                                  .min = Limits::MIN_RAMP_RATE,
                                                  .max = Limits::MAX_RAMP_RATE,
                                                  .errorMsg = "Step ramp rate out of range (0.1..10.0°C/s)"}};

    ValidationResult res;
    if (!checkFloatRules(floatRules, res)) {
        return res;
    }

    if (time > Limits::MAX_STEP_TIME_S) {
        return {.valid = false, .errorMessage = "Step hold time exceeds maximum allowed duration (600s)"};
    }

    return {.valid = true, .errorMessage = nullptr};
}

// ============================================================================
// ReflowProfile Validation
// ============================================================================

ValidationResult ReflowProfile::validate() const {
    if (name.empty() || name.length() > Limits::MAX_NAME_LENGTH) {
        return {.valid = false, .errorMessage = "Profile name must be between 1 and 30 characters"};
    }

    ValidationResult res;
    if (!checkFilename(file, "Profile file name contains illegal path traversal characters",
                       "Profile file name exceeds maximum length (40 chars)", res)) {
        return res;
    }

    if (stepsTop.empty() && stepsBottom.empty()) {
        return {.valid = false, .errorMessage = "Profile must contain at least one step in Top or Bottom heater"};
    }

    if (stepsTop.size() > Limits::MAX_PROFILE_STEPS || stepsBottom.size() > Limits::MAX_PROFILE_STEPS) {
        return {.valid = false, .errorMessage = "Profile step count exceeds maximum limit (10 steps per heater)"};
    }

    if (!validateElements(stepsTop, res)) {
        return res;
    }

    if (!validateElements(stepsBottom, res)) {
        return res;
    }

    return {.valid = true, .errorMessage = nullptr};
}

// ============================================================================
// PidPoint Validation
// ============================================================================

ValidationResult PidPoint::validate() const {
    const std::array rules = {FloatRangeRule{.val = temp,
                                             .min = Limits::MIN_TEMPERATURE,
                                             .max = Limits::MAX_TEMPERATURE,
                                             .errorMsg = "PID point temperature out of range (20..300°C)"},
                              FloatRangeRule{.val = kp,
                                             .min = Limits::MIN_PID_KP,
                                             .max = Limits::MAX_PID_KP,
                                             .errorMsg = "PID point Kp out of range (0..100)"},
                              FloatRangeRule{.val = ki,
                                             .min = Limits::MIN_PID_KI,
                                             .max = Limits::MAX_PID_KI,
                                             .errorMsg = "PID point Ki out of range (0..10)"},
                              FloatRangeRule{.val = kd,
                                             .min = Limits::MIN_PID_KD,
                                             .max = Limits::MAX_PID_KD,
                                             .errorMsg = "PID point Kd out of range (0..100)"}};

    ValidationResult res;
    if (!checkFloatRules(rules, res)) {
        return res;
    }

    return {.valid = true, .errorMessage = nullptr};
}

// ============================================================================
// PidLibrary Validation
// ============================================================================

ValidationResult PidLibrary::validate() const {
    if (top.size() > Limits::MAX_PID_POINTS || bottom.size() > Limits::MAX_PID_POINTS) {
        return {.valid = false, .errorMessage = "PID Library point count exceeds maximum limit (15 points per heater)"};
    }

    ValidationResult res;
    if (!validateElements(top, res)) {
        return res;
    }

    if (!validateElements(bottom, res)) {
        return res;
    }

    // Strict monotonic ascending order check for gain scheduling interpolation
    if (!isStrictlyIncreasing(top, "Top PID library points must have strictly increasing temperatures", res)) {
        return res;
    }

    if (!isStrictlyIncreasing(bottom, "Bottom PID library points must have strictly increasing temperatures", res)) {
        return res;
    }

    return {.valid = true, .errorMessage = nullptr};
}

// ============================================================================
// MachineSettings Validation (Tabular Validation Engine)
// ============================================================================

ValidationResult MachineSettings::validate() const {
    // 1. UI & String Constraints
    ValidationResult res;
    if (!checkFilename(defaultProfile, "Default profile file name contains illegal path traversal characters",
                       "Default profile file name exceeds maximum length (40 chars)", res)) {
        return res;
    }

    if (!language.empty() && language != "en" && language != "de") {
        return {.valid = false, .errorMessage = "Language must be 'en' or 'de'"};
    }

    if (!theme.empty() && theme != "light" && theme != "dark") {
        return {.valid = false, .errorMessage = "Theme must be 'light' or 'dark'"};
    }

    // 2. Core Float Parameter Boundary Table
    const std::array coreFloatRules = {
        FloatRangeRule{.val = maxTempTop,
                       .min = Limits::MIN_TEMPERATURE,
                       .max = Limits::MAX_TEMPERATURE,
                       .errorMsg = "Max Top temperature out of range (20..300°C)"},
        FloatRangeRule{.val = maxTempBottom,
                       .min = Limits::MIN_TEMPERATURE,
                       .max = Limits::MAX_TEMPERATURE,
                       .errorMsg = "Max Bottom temperature out of range (20..300°C)"},
        FloatRangeRule{.val = coolingSafeTemp,
                       .min = Limits::MIN_SAFE_COOLING_TEMP,
                       .max = Limits::MAX_SAFE_COOLING_TEMP,
                       .errorMsg = "Cooling safe temperature out of range (20..100°C)"},
        FloatRangeRule{.val = holdLowTolerance,
                       .min = Limits::MIN_HOLD_TOLERANCE,
                       .max = Limits::MAX_HOLD_TOLERANCE,
                       .errorMsg = "Hold low tolerance out of range (0.5..30°C)"},
        FloatRangeRule{.val = holdHighTolerance,
                       .min = Limits::MIN_HOLD_TOLERANCE,
                       .max = Limits::MAX_HOLD_TOLERANCE,
                       .errorMsg = "Hold high tolerance out of range (0.5..30°C)"},
        FloatRangeRule{.val = emaAlpha,
                       .min = Limits::MIN_EMA_ALPHA,
                       .max = Limits::MAX_EMA_ALPHA,
                       .errorMsg = "EMA alpha filter factor out of range (0.01..1.0)"},
        FloatRangeRule{.val = topCjOffset,
                       .min = Limits::MIN_CJ_OFFSET,
                       .max = Limits::MAX_CJ_OFFSET,
                       .errorMsg = "Top cold-junction offset out of range (-8.0..+7.9°C)"},
        FloatRangeRule{.val = bottomCjOffset,
                       .min = Limits::MIN_CJ_OFFSET,
                       .max = Limits::MAX_CJ_OFFSET,
                       .errorMsg = "Bottom cold-junction offset out of range (-8.0..+7.9°C)"}};

    if (!checkFloatRules(coreFloatRules, res)) {
        return res;
    }

    // Min temperature cross-boundary checks (must not exceed max temp)
    if (std::isnan(minTempTop) || minTempTop < Limits::MIN_UNDERTEMP_LIMIT ||
        minTempTop > Limits::MAX_UNDERTEMP_LIMIT || minTempTop > maxTempTop) {
        return {.valid = false,
                .errorMessage = "Min Top temperature out of range (-20..50°C) or exceeds Max Top temperature"};
    }

    if (std::isnan(minTempBottom) || minTempBottom < Limits::MIN_UNDERTEMP_LIMIT ||
        minTempBottom > Limits::MAX_UNDERTEMP_LIMIT || minTempBottom > maxTempBottom) {
        return {.valid = false,
                .errorMessage = "Min Bottom temperature out of range (-20..50°C) or exceeds Max Bottom temperature"};
    }

    // 3. Conditional Watchdog Rules
    if (enableStuckSsrCheck) {
        const std::array stuckFloatRules = {
            FloatRangeRule{.val = stuckSsrRiseThreshold,
                           .min = Limits::MIN_STUCK_SSR_RISE,
                           .max = Limits::MAX_STUCK_SSR_RISE,
                           .errorMsg = "Stuck SSR rise threshold out of range (1..50°C)"}};
        if (!checkFloatRules(stuckFloatRules, res)) {
            return res;
        }

        const std::array stuckUintRules = {UintRangeRule{.val = stuckSsrWindowSec,
                                                         .min = Limits::MIN_STUCK_SSR_SEC,
                                                         .max = Limits::MAX_STUCK_SSR_SEC,
                                                         .errorMsg = "Stuck SSR window out of range (3..120s)"}};
        if (!checkUintRules(stuckUintRules, res)) {
            return res;
        }
    }

    if (enableNoRiseCheck) {
        const std::array noRiseFloatRules = {FloatRangeRule{.val = noRiseThreshold,
                                                            .min = Limits::MIN_NO_RISE_THRESH,
                                                            .max = Limits::MAX_NO_RISE_THRESH,
                                                            .errorMsg = "No-rise threshold out of range (0.5..50°C)"}};
        if (!checkFloatRules(noRiseFloatRules, res)) {
            return res;
        }

        const std::array noRiseUintRules = {UintRangeRule{.val = noRiseTimeoutSec,
                                                          .min = Limits::MIN_NO_RISE_TIMEOUT_S,
                                                          .max = Limits::MAX_NO_RISE_TIMEOUT_S,
                                                          .errorMsg = "No-rise timeout out of range (5..120s)"}};
        if (!checkUintRules(noRiseUintRules, res)) {
            return res;
        }
    }

    // 4. Core Integer Timing Boundary Table
    const std::array timingRules = {UintRangeRule{.val = settleTimeS,
                                                  .min = Limits::MIN_SETTLE_S,
                                                  .max = Limits::MAX_SETTLE_S,
                                                  .errorMsg = "Settle duration out of range (1..60s)"},
                                    UintRangeRule{.val = fanCoolingDelayS,
                                                  .min = Limits::MIN_FAN_DELAY_S,
                                                  .max = Limits::MAX_FAN_DELAY_S,
                                                  .errorMsg = "Fan cooling delay out of range (0..300s)"},
                                    UintRangeRule{.val = fanCoolingDurationS,
                                                  .min = Limits::MIN_FAN_DURATION_S,
                                                  .max = Limits::MAX_FAN_DURATION_S,
                                                  .errorMsg = "Fan cooling duration out of range (0..600s)"},
                                    UintRangeRule{.val = preheatHoldTimeoutMin,
                                                  .min = Limits::MIN_PREHEAT_HOLD_TIMEOUT_MIN,
                                                  .max = Limits::MAX_PREHEAT_HOLD_TIMEOUT_MIN,
                                                  .errorMsg = "Preheat hold timeout out of range (0..180min)"},
                                    UintRangeRule{.val = faultStreakLimit,
                                                  .min = Limits::MIN_FAULT_STREAK,
                                                  .max = Limits::MAX_FAULT_STREAK,
                                                  .errorMsg = "Fault streak limit out of range (1..20)"},
                                    UintRangeRule{.val = topBurstWindowMs,
                                                  .min = Limits::MIN_BURST_WINDOW_MS,
                                                  .max = Limits::MAX_BURST_WINDOW_MS,
                                                  .errorMsg = "Top burst window out of range (500..4000ms)"},
                                    UintRangeRule{.val = bottomBurstWindowMs,
                                                  .min = Limits::MIN_BURST_WINDOW_MS,
                                                  .max = Limits::MAX_BURST_WINDOW_MS,
                                                  .errorMsg = "Bottom burst window out of range (500..4000ms)"}};

    if (!checkUintRules(timingRules, res)) {
        return res;
    }

    // 5. Default PID Gains Boundary Table
    const std::array pidRules = {FloatRangeRule{.val = topKp,
                                                .min = Limits::MIN_PID_KP,
                                                .max = Limits::MAX_PID_KP,
                                                .errorMsg = "Top PID default gains out of allowable range"},
                                 FloatRangeRule{.val = topKi,
                                                .min = Limits::MIN_PID_KI,
                                                .max = Limits::MAX_PID_KI,
                                                .errorMsg = "Top PID default gains out of allowable range"},
                                 FloatRangeRule{.val = topKd,
                                                .min = Limits::MIN_PID_KD,
                                                .max = Limits::MAX_PID_KD,
                                                .errorMsg = "Top PID default gains out of allowable range"},
                                 FloatRangeRule{.val = bottomKp,
                                                .min = Limits::MIN_PID_KP,
                                                .max = Limits::MAX_PID_KP,
                                                .errorMsg = "Bottom PID default gains out of allowable range"},
                                 FloatRangeRule{.val = bottomKi,
                                                .min = Limits::MIN_PID_KI,
                                                .max = Limits::MAX_PID_KI,
                                                .errorMsg = "Bottom PID default gains out of allowable range"},
                                 FloatRangeRule{.val = bottomKd,
                                                .min = Limits::MIN_PID_KD,
                                                .max = Limits::MAX_PID_KD,
                                                .errorMsg = "Bottom PID default gains out of allowable range"}};

    if (!checkFloatRules(pidRules, res)) {
        return res;
    }

    return {.valid = true, .errorMessage = nullptr};
}

} // namespace config
