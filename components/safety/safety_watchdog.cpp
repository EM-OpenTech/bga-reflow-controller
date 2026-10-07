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
 * stuck SSR conditions, and heater failure (no-rise) states.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "safety/safety_watchdog.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

static const char* TAG = "SAFETY";

namespace safety {

// ============================================================================
// Lifecycle & State Initialization
// ============================================================================

SafetyWatchdog::SafetyWatchdog(output::OutputManager& outputManager, const SafetyConfig& config)
    : _outputManager(outputManager),
      _config(config),
      _fault(SafetyFault::NONE) {}

void SafetyWatchdog::begin() {
    reset();
}

void SafetyWatchdog::reset() {
    _fault       = SafetyFault::NONE;
    _topState    = ChannelWatchdogState();
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

bool SafetyWatchdog::check(const sensor::SensorReading& topReading,
                           const sensor::SensorReading& bottomReading,
                           float topPower, float bottomPower,
                           float topSetpoint, float bottomSetpoint) {
    // If master safety watchdog is disabled (testbench bypass mode), return clean state
    if (!_config.enableSafetyWatchdog) {
        _outputManager.setInhibit(false);
        return false;
    }

    // If already in latched fault state, maintain hardware inhibit
    if (_fault != SafetyFault::NONE) {
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

bool SafetyWatchdog::checkChannel(const sensor::SensorReading& reading,
                                  float power,
                                  float setpoint,
                                  bool isTop,
                                  ChannelWatchdogState& state) {
    uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());

    // 0. Floating-Point Hygiene (NaN / Inf protection)
    if (std::isnan(reading.temperature) || std::isinf(reading.temperature)) {
        _fault = isTop ? SafetyFault::SENSOR_FAULT_TOP : SafetyFault::SENSOR_FAULT_BOTTOM;
        _outputManager.setInhibit(true);
        ESP_LOGE(TAG, "CRITICAL: %s Sensor temperature is NaN or Inf!", isTop ? "TOP" : "BOTTOM");
        return true;
    }

    // 1. Sensor Reading Validity Check (Open wire / SPI transaction failure)
    if (!reading.isValid) {
        _fault = isTop ? SafetyFault::SENSOR_FAULT_TOP : SafetyFault::SENSOR_FAULT_BOTTOM;
        _outputManager.setInhibit(true);
        ESP_LOGE(TAG, "CRITICAL: %s Sensor Fault detected!", isTop ? "TOP" : "BOTTOM");
        return true;
    }

    // 2. Layer-2 Hardware IC Fault Check (Direct MAX31856 Register 0x0F inspection)
    if (reading.fault.hasFault()) {
        _fault = isTop ? SafetyFault::HARDWARE_IC_FAULT_TOP : SafetyFault::HARDWARE_IC_FAULT_BOTTOM;
        _outputManager.setInhibit(true);
        ESP_LOGE(TAG, "CRITICAL: %s MAX31856 IC Hardware Fault! (0x%02X)",
                 isTop ? "TOP" : "BOTTOM", reading.fault.rawByte);
        return true;
    }

    // 3. Absolute Overtemperature Check (Software limit, default 280°C)
    float maxAllowed = isTop ? _config.maxTempTop : _config.maxTempBottom;
    if (reading.temperature > maxAllowed) {
        _fault = isTop ? SafetyFault::OVERTEMP_TOP : SafetyFault::OVERTEMP_BOTTOM;
        _outputManager.setInhibit(true);
        ESP_LOGE(TAG, "CRITICAL: %s OVERTEMPERATURE! Temp: %.1f°C > Limit: %.1f°C",
                 isTop ? "TOP" : "BOTTOM", reading.temperature, maxAllowed);
        return true;
    }

    // 4. Absolute Undertemperature Check (Software limit, default 5°C)
    if (_config.enableMinTempCheck) {
        float minAllowed = isTop ? _config.minTempTop : _config.minTempBottom;
        if (reading.temperature < minAllowed) {
            _fault = isTop ? SafetyFault::UNDERTEMP_TOP : SafetyFault::UNDERTEMP_BOTTOM;
            _outputManager.setInhibit(true);
            ESP_LOGE(TAG, "CRITICAL: %s UNDERTEMPERATURE! Temp: %.1f°C < Limit: %.1f°C",
                     isTop ? "TOP" : "BOTTOM", reading.temperature, minAllowed);
            return true;
        }
    }

    // 5. Stuck SSR Detection (Power requested is 0% in hot state > 50°C, but temp keeps rising)
    if (_config.enableStuckSsrCheck && power < 1.0f && reading.temperature > 50.0f) {
        if (!state.stuckActive) {
            state.stuckStartTemp = reading.temperature;
            state.stuckStartUs   = nowUs;
            state.stuckActive    = true;
        } else {
            uint64_t elapsedUs = nowUs - state.stuckStartUs;
            uint64_t windowUs  = static_cast<uint64_t>(_config.stuckSsrWindowSec) * 1000000ULL;
            if (elapsedUs >= windowUs) {
                if (reading.temperature - state.stuckStartTemp > _config.stuckSsrRiseThreshold) {
                    _fault = isTop ? SafetyFault::STUCK_SSR_TOP : SafetyFault::STUCK_SSR_BOTTOM;
                    _outputManager.setInhibit(true);
                    ESP_LOGE(TAG, "CRITICAL: %s STUCK SSR detected! Temp rose %.1f°C with 0%% power",
                             isTop ? "TOP" : "BOTTOM", reading.temperature - state.stuckStartTemp);
                    return true;
                }
                state.stuckActive = false; // Reset window for next check
            }
        }
    } else {
        state.stuckActive = false;
    }

    // 6. Dynamic Heater Failure / No-Rise Detection (Power requested is 100%, but temp fails to rise)
    // DYNAMIC PCB FIX:
    // We compare actual temperature against the ACTIVE SETPOINT or the Max Safety Limit.
    // If current temperature is already within 20°C of setpoint (or within 20°C of Max Safety Limit),
    // the heater has ALREADY brought the PCB up to target temperature! The No-Rise check is bypassed,
    // preventing false alarms regardless of whether it's a Leaded PCB (210°C) or Lead-Free PCB (245°C).
    bool isNearTarget = (setpoint > 0.0f && (setpoint - reading.temperature < 20.0f)) ||
                        (reading.temperature > (maxAllowed - 20.0f));

    if (_config.enableNoRiseCheck && power > 99.0f && !isNearTarget) {
        if (!state.noRiseActive) {
            state.noRiseStartTemp = reading.temperature;
            state.noRiseStartUs   = nowUs;
            state.noRiseActive    = true;
        } else {
            uint64_t elapsedUs = nowUs - state.noRiseStartUs;
            uint64_t timeoutUs = static_cast<uint64_t>(_config.noRiseTimeoutSec) * 1000000ULL;
            if (elapsedUs >= timeoutUs) {
                if (reading.temperature - state.noRiseStartTemp < _config.noRiseThreshold) {
                    _fault = isTop ? SafetyFault::NO_RISE_TOP : SafetyFault::NO_RISE_BOTTOM;
                    _outputManager.setInhibit(true);
                    ESP_LOGE(TAG, "CRITICAL: %s HEATER FAILURE! Power 100%% but temp rose < %.1f°C in %lus (Temp: %.1f°C, Setpoint: %.1f°C)",
                             isTop ? "TOP" : "BOTTOM", _config.noRiseThreshold,
                             (unsigned long)_config.noRiseTimeoutSec, reading.temperature, setpoint);
                    return true;
                }
                state.noRiseActive = false; // Reset window
            }
        }
    } else {
        // Hysteresis: reset check when power drops below 80% or when near target
        if (power < 80.0f || isNearTarget) {
            state.noRiseActive = false;
        }
    }

    return false;
}

// ============================================================================
// Human-Readable Fault String Formatting
// ============================================================================

const char* SafetyWatchdog::getFaultString() const {
    switch (_fault) {
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
