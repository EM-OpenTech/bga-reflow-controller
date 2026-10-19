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
 * @file safety_watchdog.cpp
 * @brief Implementation of hardware & thermal safety watchdog.
 *
 * Evaluates real-time sensor validity, IC fault flags, temperature limits,
 * stuck SSR conditions, and heater failure (no-rise) states using linear
 * guard clauses and decoupled safety primitives.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "safety/safety_watchdog.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <array>
#include <cmath>
#include <cstdio>

namespace safety {

namespace {

constexpr const char* TAG = "SAFETY";

/// Threshold margin in °C to consider sensor temperature near target setpoint or safety ceiling
constexpr float TARGET_PROXIMITY_MARGIN_DEG = 20.0f;

/// Conversion factor from seconds to microseconds
constexpr uint64_t SEC_TO_US = 1000000ULL;

/**
 * @brief Checks if current temperature is close to the active setpoint or the hardware ceiling.
 *
 * Used by No-Rise detection to prevent false alarms near profile plateaus where heating rate
 * naturally flattens.
 */
bool isTemperatureNearTarget(float temp, float setpoint, float maxAllowed) {
    if (setpoint > 0.0f && (setpoint - temp < TARGET_PROXIMITY_MARGIN_DEG)) {
        return true;
    }
    return temp > (maxAllowed - TARGET_PROXIMITY_MARGIN_DEG);
}

} // anonymous namespace

// ============================================================================
// Lifecycle & State Initialization
// ============================================================================

SafetyWatchdog::SafetyWatchdog(output::OutputManager& outputManager, const SafetyConfig& config)
    : _outputManager(outputManager), _config(config), _fault(SafetyFault::NONE) {}

void SafetyWatchdog::begin() {
    reset();
}

void SafetyWatchdog::reset() {
    _fault.store(SafetyFault::NONE, std::memory_order_relaxed);
    _topState = ChannelWatchdogState();
    _bottomState = ChannelWatchdogState();
    _outputManager.setInhibit(false);
    ESP_LOGI(TAG, "SafetyWatchdog initialized/reset. Inhibit RELEASED.");
}

void SafetyWatchdog::updateConfig(const SafetyConfig& config) {
    _config = config;
    ESP_LOGI(TAG, "SafetyWatchdog configuration updated.");
}

// ============================================================================
// Real-Time Safety Evaluation Loop
// ============================================================================

bool SafetyWatchdog::check(const sensor::SensorReading& topReading, const sensor::SensorReading& bottomReading,
                           float topPower, float bottomPower, float topSetpoint, float bottomSetpoint) {
    // If master safety watchdog is disabled (testbench bypass mode), release inhibit and return safe
    if (!_config.enableSafetyWatchdog) {
        _outputManager.setInhibit(false);
        return false;
    }

    // If already in latched fault state, maintain hardware inhibit
    if (_fault.load(std::memory_order_relaxed) != SafetyFault::NONE) {
        _outputManager.setInhibit(true);
        return true;
    }

    // Check Top Channel
    if (checkChannel(topReading, topPower, topSetpoint, true, _topState)) {
        return true;
    }

    // Check Bottom Channel
    if (checkChannel(bottomReading, bottomPower, bottomSetpoint, false, _bottomState)) {
        return true;
    }

    return false;
}

void SafetyWatchdog::tripFault(SafetyFault fault, const char* logMsg) {
    _fault.store(fault, std::memory_order_relaxed);
    _outputManager.setInhibit(true);
    ESP_LOGE(TAG, "%s", logMsg);
}

bool SafetyWatchdog::checkSensorHealth(const sensor::SensorReading& reading, bool isTop) {
    // 0. Floating-Point Hygiene (NaN / Inf protection)
    if (std::isnan(reading.temperature) || std::isinf(reading.temperature)) {
        tripFault(isTop ? SafetyFault::SENSOR_FAULT_TOP : SafetyFault::SENSOR_FAULT_BOTTOM,
                  isTop ? "CRITICAL: TOP Sensor temperature is NaN or Inf!"
                        : "CRITICAL: BOTTOM Sensor temperature is NaN or Inf!");
        return true;
    }

    // 1. Sensor Reading Validity Check (Open wire / SPI transaction failure)
    if (!reading.isValid) {
        tripFault(isTop ? SafetyFault::SENSOR_FAULT_TOP : SafetyFault::SENSOR_FAULT_BOTTOM,
                  isTop ? "CRITICAL: TOP Sensor Fault detected!" : "CRITICAL: BOTTOM Sensor Fault detected!");
        return true;
    }

    // 2. Layer-2 Hardware IC Fault Check (Direct MAX31856 Register 0x0F inspection)
    if (reading.fault.hasFault()) {
        std::array<char, 64> buf{};
        (void)snprintf(buf.data(), buf.size(), "CRITICAL: %s MAX31856 IC Hardware Fault! (0x%02X)",
                       isTop ? "TOP" : "BOTTOM", reading.fault.rawByte);
        tripFault(isTop ? SafetyFault::HARDWARE_IC_FAULT_TOP : SafetyFault::HARDWARE_IC_FAULT_BOTTOM, buf.data());
        return true;
    }

    return false;
}

bool SafetyWatchdog::checkTemperatureLimits(float temp, float maxAllowed, float minAllowed, bool isTop) {
    // 3. Absolute Overtemperature Check
    if (temp > maxAllowed) {
        std::array<char, 96> buf{};
        (void)snprintf(buf.data(), buf.size(), "CRITICAL: %s OVERTEMPERATURE! Temp: %.1f°C > Limit: %.1f°C",
                       isTop ? "TOP" : "BOTTOM", static_cast<double>(temp), static_cast<double>(maxAllowed));
        tripFault(isTop ? SafetyFault::OVERTEMP_TOP : SafetyFault::OVERTEMP_BOTTOM, buf.data());
        return true;
    }

    // 4. Absolute Undertemperature Check
    if (_config.enableMinTempCheck && temp < minAllowed) {
        std::array<char, 96> buf{};
        (void)snprintf(buf.data(), buf.size(), "CRITICAL: %s UNDERTEMPERATURE! Temp: %.1f°C < Limit: %.1f°C",
                       isTop ? "TOP" : "BOTTOM", static_cast<double>(temp), static_cast<double>(minAllowed));
        tripFault(isTop ? SafetyFault::UNDERTEMP_TOP : SafetyFault::UNDERTEMP_BOTTOM, buf.data());
        return true;
    }

    return false;
}

bool SafetyWatchdog::checkStuckSsr(float temp, float power, bool isTop, ChannelWatchdogState& state, uint64_t nowUs) {
    if (!_config.enableStuckSsrCheck || power >= 1.0f || temp <= 50.0f) {
        state.stuckActive = false;
        return false;
    }

    if (!state.stuckActive) {
        state.stuckStartTemp = temp;
        state.stuckStartUs = nowUs;
        state.stuckActive = true;
        return false;
    }

    uint64_t elapsedUs = nowUs - state.stuckStartUs;
    uint64_t windowUs = static_cast<uint64_t>(_config.stuckSsrWindowSec) * SEC_TO_US;

    if (elapsedUs < windowUs) {
        return false;
    }

    float rise = temp - state.stuckStartTemp;
    if (rise > _config.stuckSsrRiseThreshold) {
        std::array<char, 96> buf{};
        (void)snprintf(buf.data(), buf.size(), "CRITICAL: %s STUCK SSR detected! Temp rose %.1f°C with 0%% power",
                       isTop ? "TOP" : "BOTTOM", static_cast<double>(rise));
        tripFault(isTop ? SafetyFault::STUCK_SSR_TOP : SafetyFault::STUCK_SSR_BOTTOM, buf.data());
        return true;
    }

    state.stuckActive = false; // Reset window for next check cycle
    return false;
}

bool SafetyWatchdog::checkNoRise(float temp, float power, float setpoint, float maxAllowed, bool isTop,
                                 ChannelWatchdogState& state, uint64_t nowUs) {
    bool isNearTarget = isTemperatureNearTarget(temp, setpoint, maxAllowed);

    if (!_config.enableNoRiseCheck || power <= 99.0f || isNearTarget) {
        if (power < 80.0f || isNearTarget) {
            state.noRiseActive = false;
        }
        return false;
    }

    if (!state.noRiseActive) {
        state.noRiseStartTemp = temp;
        state.noRiseStartUs = nowUs;
        state.noRiseActive = true;
        return false;
    }

    uint64_t elapsedUs = nowUs - state.noRiseStartUs;
    uint64_t timeoutUs = static_cast<uint64_t>(_config.noRiseTimeoutSec) * SEC_TO_US;

    if (elapsedUs < timeoutUs) {
        return false;
    }

    float rise = temp - state.noRiseStartTemp;
    if (rise < _config.noRiseThreshold) {
        std::array<char, 128> buf{};
        (void)snprintf(buf.data(), buf.size(),
                       "CRITICAL: %s HEATER FAILURE! Power 100%% but temp rose < %.1f°C in %lus",
                       isTop ? "TOP" : "BOTTOM", static_cast<double>(_config.noRiseThreshold),
                       static_cast<unsigned long>(_config.noRiseTimeoutSec));
        tripFault(isTop ? SafetyFault::NO_RISE_TOP : SafetyFault::NO_RISE_BOTTOM, buf.data());
        return true;
    }

    state.noRiseActive = false; // Reset window
    return false;
}

bool SafetyWatchdog::checkChannel(const sensor::SensorReading& reading, float power, float setpoint, bool isTop,
                                  ChannelWatchdogState& state) {
    const auto nowUs = static_cast<uint64_t>(esp_timer_get_time());
    float maxAllowed = isTop ? _config.maxTempTop : _config.maxTempBottom;
    float minAllowed = isTop ? _config.minTempTop : _config.minTempBottom;

    if (checkSensorHealth(reading, isTop)) {
        return true;
    }
    if (checkTemperatureLimits(reading.temperature, maxAllowed, minAllowed, isTop)) {
        return true;
    }
    if (checkStuckSsr(reading.temperature, power, isTop, state, nowUs)) {
        return true;
    }
    if (checkNoRise(reading.temperature, power, setpoint, maxAllowed, isTop, state, nowUs)) {
        return true;
    }

    return false;
}

// ============================================================================
// Human-Readable Fault String Formatting
// ============================================================================

const char* SafetyWatchdog::getFaultString() const {
    switch (_fault.load(std::memory_order_relaxed)) {
    case SafetyFault::NONE:
        return "No Fault";
    case SafetyFault::SENSOR_FAULT_TOP:
        return "FAULT: Top sensor reading invalid (Check MAX31856 wiring)";
    case SafetyFault::SENSOR_FAULT_BOTTOM:
        return "FAULT: Bottom sensor reading invalid (Check MAX31856 wiring)";
    case SafetyFault::OVERTEMP_TOP:
        return "FAULT: Top heater overtemperature limit exceeded!";
    case SafetyFault::OVERTEMP_BOTTOM:
        return "FAULT: Bottom heater overtemperature limit exceeded!";
    case SafetyFault::UNDERTEMP_TOP:
        return "FAULT: Top heater undertemperature limit (< 5.0°C)!";
    case SafetyFault::UNDERTEMP_BOTTOM:
        return "FAULT: Bottom heater undertemperature limit (< 5.0°C)!";
    case SafetyFault::STUCK_SSR_TOP:
        return "FAULT: Top SSR stuck in ON state! (Heater shorted)";
    case SafetyFault::STUCK_SSR_BOTTOM:
        return "FAULT: Bottom SSR stuck in ON state! (Heater shorted)";
    case SafetyFault::NO_RISE_TOP:
        return "FAULT: Top heater failed to heat (Check SSR / Heating element)";
    case SafetyFault::NO_RISE_BOTTOM:
        return "FAULT: Bottom heater failed to heat (Check SSR / Heating element)";
    case SafetyFault::HARDWARE_IC_FAULT_TOP:
        return "FAULT: Top MAX31856 IC Hardware Flag set!";
    case SafetyFault::HARDWARE_IC_FAULT_BOTTOM:
        return "FAULT: Bottom MAX31856 IC Hardware Flag set!";
    default:
        return "FAULT: Unknown safety error";
    }
}

} // namespace safety
