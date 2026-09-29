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
 * @file safety_watchdog.hpp
 * @brief Hardware & thermal safety watchdog for dual-channel BGA reflow controller.
 *
 * Provides real-time physical monitoring for:
 *   - Thermocouple open-circuit & SPI communication faults
 *   - Layer-2 hardware IC fault flags (MAX31856 register 0x0F)
 *   - Absolute over-temperature and under-temperature limits
 *   - Stuck / shorted SSR detection (thermal rise during 0% power)
 *   - Heater failure / no-rise detection (0°C rise during 100% power)
 *
 * When a critical fault condition occurs, SafetyWatchdog immediately triggers
 * hardware output inhibition via OutputManager::setInhibit(true).
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include <cstdint>
#include "esp_timer.h"
#include "sensor/max31856.hpp"
#include "output/output_manager.hpp"

namespace safety {

// ============================================================================
// GLOBAL CONFIGURATION CONSTANTS
// Default safety limits for ceramic / quartz heater reflow stations
// ============================================================================

/// Default maximum allowed temperature for Top Heater (°C)
constexpr float DEFAULT_MAX_TEMP_TOP = 280.0f;

/// Default maximum allowed temperature for Bottom Heater (°C)
constexpr float DEFAULT_MAX_TEMP_BOTTOM = 280.0f;

/// Default minimum allowed temperature for Top Heater (°C)
constexpr float DEFAULT_MIN_TEMP_TOP = 5.0f;

/// Default minimum allowed temperature for Bottom Heater (°C)
constexpr float DEFAULT_MIN_TEMP_BOTTOM = 5.0f;

/// Default temperature rise threshold to detect a Stuck SSR (°C)
constexpr float DEFAULT_STUCK_SSR_RISE_THRESHOLD = 5.0f;

/// Default time window to evaluate Stuck SSR condition (seconds)
constexpr uint32_t DEFAULT_STUCK_SSR_WINDOW_SEC = 10;

/// Default minimum temperature rise required when heater is running at 100% (°C)
constexpr float DEFAULT_NO_RISE_THRESHOLD = 3.0f;

/// Default timeout duration for No-Rise heater failure detection (seconds)
constexpr uint32_t DEFAULT_NO_RISE_TIMEOUT_SEC = 15;

// ============================================================================
// SAFETY ENUMS & STRUCTURES
// ============================================================================

/**
 * @brief Enumeration of all hardware & physical thermal safety fault conditions.
 */
enum class SafetyFault : uint8_t {
    NONE = 0,               ///< System operating normally, no fault
    SENSOR_FAULT_TOP,       ///< Top MAX31856 thermocouple reading invalid (wire break / SPI error)
    SENSOR_FAULT_BOTTOM,    ///< Bottom MAX31856 thermocouple reading invalid (wire break / SPI error)
    OVERTEMP_TOP,           ///< Top heater actual temp exceeded maximum limit (e.g. > 280°C)
    OVERTEMP_BOTTOM,        ///< Bottom heater actual temp exceeded maximum limit (e.g. > 280°C)
    UNDERTEMP_TOP,          ///< Top heater actual temp below minimum limit (e.g. < 5°C)
    UNDERTEMP_BOTTOM,       ///< Bottom heater actual temp below minimum limit (e.g. < 5°C)
    STUCK_SSR_TOP,          ///< Top SSR stuck in ON state (temperature rises while power is 0%)
    STUCK_SSR_BOTTOM,       ///< Bottom SSR stuck in ON state (temperature rises while power is 0%)
    NO_RISE_TOP,            ///< Top heater failure (0°C rise after 100% power for timeout period)
    NO_RISE_BOTTOM,         ///< Bottom heater failure (0°C rise after 100% power for timeout period)
    HARDWARE_IC_FAULT_TOP,  ///< Layer-2 hardware fault reported directly by Top MAX31856 IC
    HARDWARE_IC_FAULT_BOTTOM///< Layer-2 hardware fault reported directly by Bottom MAX31856 IC
};

/**
 * @brief Configuration Structure for Hardware Safety Watchdog limits.
 */
struct SafetyConfig {
    float    maxTempTop            = DEFAULT_MAX_TEMP_TOP;          ///< Maximum allowed temperature for top heater (°C)
    float    maxTempBottom         = DEFAULT_MAX_TEMP_BOTTOM;       ///< Maximum allowed temperature for bottom heater (°C)
    float    minTempTop            = DEFAULT_MIN_TEMP_TOP;          ///< Minimum allowed temperature for top heater (°C)
    float    minTempBottom         = DEFAULT_MIN_TEMP_BOTTOM;       ///< Minimum allowed temperature for bottom heater (°C)

    float    stuckSsrRiseThreshold = DEFAULT_STUCK_SSR_RISE_THRESHOLD; ///< Rise threshold to trigger stuck SSR fault (°C)
    uint32_t stuckSsrWindowSec     = DEFAULT_STUCK_SSR_WINDOW_SEC;  ///< Evaluation time window for stuck SSR detection (s)

    float    noRiseThreshold       = DEFAULT_NO_RISE_THRESHOLD;     ///< Minimum rise required when heater runs at 100% (°C)
    uint32_t noRiseTimeoutSec      = DEFAULT_NO_RISE_TIMEOUT_SEC;   ///< Timeout before declaring heater failure / no-rise (s)

    bool     enableStuckSsrCheck   = true;                          ///< Enable stuck SSR detection
    bool     enableNoRiseCheck     = true;                          ///< Enable no-rise (heater failure) detection
    bool     enableMinTempCheck    = true;                          ///< Enable undertemperature detection
};

/**
 * @brief Internal tracking state for a single heater channel watchdog.
 */
struct ChannelWatchdogState {
    bool     stuckActive        = false; ///< True if stuck SSR evaluation window is active
    float    stuckStartTemp     = 0.0f;  ///< Temperature at start of stuck SSR window (°C)
    uint64_t stuckStartUs       = 0;     ///< Timestamp when stuck SSR window started (µs)

    bool     noRiseActive       = false; ///< True if no-rise evaluation window is active
    float    noRiseStartTemp    = 0.0f;  ///< Temperature at start of no-rise window (°C)
    uint64_t noRiseStartUs      = 0;     ///< Timestamp when no-rise window started (µs)
};

/**
 * @class SafetyWatchdog
 * @brief Real-time hardware and thermal safety watchdog.
 * 
 * Monitored Safety Conditions:
 *   1. Sensor Faults: Open circuit, SPI transaction error, invalid reading.
 *   2. Layer-2 Hardware IC Faults: Direct detection from MAX31856 register 0x0F (tcHigh, tcLow, openCircuit, OVUV).
 *   3. Absolute Overtemperature: Immediate shutdown if temp > max limit (default: 280°C).
 *   4. Absolute Undertemperature: Immediate shutdown if temp < min limit (default: 5°C).
 *   5. Stuck SSR Detection: Detects shorted / frozen SSRs that keep heating when power = 0%.
 *   6. Heater Failure (No-Rise): Detects dead SSRs, blown heating elements, or loose sensors when power = 100%.
 * 
 * Hardware Protection:
 *   When a fault is latched, SafetyWatchdog calls OutputManager::setInhibit(true),
 *   forcing ALL SSR hardware outputs LOW immediately.
 */
class SafetyWatchdog {
public:
    /**
     * @brief Construct a new SafetyWatchdog instance.
     * @param outputManager Reference to OutputManager for hardware output inhibition.
     * @param config Initial safety configuration limits (defaults to SafetyConfig()).
     */
    SafetyWatchdog(output::OutputManager& outputManager, const SafetyConfig& config = SafetyConfig());

    /**
     * @brief Initialize the watchdog and clear any existing inhibit state.
     */
    void begin();

    /**
     * @brief Evaluate hardware safety checks for Top and Bottom heater channels.
     * @param topReading Latest Top MAX31856 reading
     * @param bottomReading Latest Bottom MAX31856 reading
     * @param topPower Current Top PID power (0-100%)
     * @param bottomPower Current Bottom PID power (0-100%)
     * @param topSetpoint Target Top setpoint temperature in °C (0.0f if idle)
     * @param bottomSetpoint Target Bottom setpoint temperature in °C (0.0f if idle)
     * @return true if a safety fault was detected and latched (outputs inhibited), false if safe / normal.
     */
    bool check(const sensor::SensorReading& topReading,
               const sensor::SensorReading& bottomReading,
               float topPower, float bottomPower,
               float topSetpoint = 0.0f, float bottomSetpoint = 0.0f);

    /**
     * @brief Reset latched safety faults, clear channel states, and release hardware output inhibition.
     */
    void reset();

    /**
     * @brief Check if a safety fault is currently latched.
     * @return true if in fault state, false otherwise.
     */
    bool hasFault() const { return _fault != SafetyFault::NONE; }

    /**
     * @brief Get the currently active safety fault code.
     * @return SafetyFault enumeration value.
     */
    SafetyFault getFault() const { return _fault; }

    /**
     * @brief Get a human-readable description of the active safety fault.
     * @return C-string describing the fault.
     */
    const char* getFaultString() const;

    /**
     * @brief Get the current safety configuration.
     * @return Const reference to active SafetyConfig.
     */
    const SafetyConfig& getConfig() const { return _config; }

    /**
     * @brief Update the active safety configuration.
     * @param config New SafetyConfig parameters to apply.
     */
    void updateConfig(const SafetyConfig& config);

private:
    output::OutputManager& _outputManager; ///< Reference to hardware output manager for SSR inhibition
    SafetyConfig           _config;        ///< Active watchdog configuration
    SafetyFault            _fault = SafetyFault::NONE; ///< Currently latched safety fault

    ChannelWatchdogState   _topState;      ///< Top heater watchdog tracking state
    ChannelWatchdogState   _bottomState;   ///< Bottom heater watchdog tracking state

    /**
     * @brief Internal helper to evaluate all safety conditions for a single channel.
     * @param reading Sensor reading of the channel.
     * @param power Current power delivered to the channel (0-100%).
     * @param setpoint Active target setpoint in °C.
     * @param isTop true for Top channel, false for Bottom channel.
     * @param state Channel-specific watchdog state tracking variables.
     * @return true if a fault was detected, false if channel is normal.
     */
    bool checkChannel(const sensor::SensorReading& reading,
                      float power,
                      float setpoint,
                      bool isTop,
                      ChannelWatchdogState& state);
};

} // namespace safety

