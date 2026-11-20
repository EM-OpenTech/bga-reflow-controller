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
 * @file machine_config.hpp
 * @brief Central configuration, data schemas, validation bounds, and domain models.
 *
 * Serves as the Single Source of Truth for system limits, step resolutions,
 * profile step structures, PID libraries, and machine settings.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "config/post_report.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace config {

// ============================================================================
// CENTRAL VALIDATION LIMITS & ARCHITECTURAL BLUEPRINT
// ============================================================================
/**
 * @namespace config::Limits
 * @brief Single Source of Truth for system-wide validation bounds and limits.
 *
 * Defines centralized limits, input constraints, and array boundaries
 * referenced across the REST API, Storage Manager, and Web Frontend.
 */
namespace Limits {
// String & Array Limits
inline constexpr size_t MAX_NAME_LENGTH = 30;           ///< Max profile & name length (chars)
inline constexpr size_t MAX_FILENAME_LENGTH = 40;       ///< Max filename length including .json (chars)
inline constexpr size_t MAX_PROFILE_STEPS = 10;         ///< Max steps per channel in a Reflow Profile
inline constexpr size_t MAX_PID_POINTS = 15;            ///< Max points per channel in PID Library
inline constexpr size_t MAX_HISTORY_POINTS = 10800;     ///< Max telemetry history points (3 hours at 1 Hz)
inline constexpr size_t MAX_JSON_PAYLOAD_BYTES = 65536; ///< 64 KB max JSON payload size
inline constexpr size_t MAX_ZIP_PAYLOAD_BYTES = 524288; ///< 512 KB max ZIP backup size

// Storage & Flash Wear-Leveling Safeguards (%)
inline constexpr float STORAGE_WARN_PERCENT = 80.0f; ///< LittleFS wear-leveling advisory threshold (%)
inline constexpr float STORAGE_MAX_PERCENT = 90.0f;  ///< LittleFS hard limit to preserve wear-leveling blocks (%)

// Temperature Boundaries (°C)
inline constexpr float MIN_TEMPERATURE = 20.0f;             ///< Absolute minimum allowed target temp
inline constexpr float MAX_TEMPERATURE = 300.0f;            ///< Absolute maximum allowed target temp
inline constexpr float FAILSAFE_DISCONNECTED_TEMP = 999.0f; ///< Sentinel temp when sensor disconnected/fault (°C)
inline constexpr float MIN_SAFE_COOLING_TEMP = 20.0f;       ///< Min allowable safe cooling threshold
inline constexpr float MAX_SAFE_COOLING_TEMP = 100.0f;      ///< Max allowable safe cooling threshold
inline constexpr float MIN_UNDERTEMP_LIMIT = -20.0f;        ///< Min allowable undertemp fault threshold
inline constexpr float MAX_UNDERTEMP_LIMIT = 50.0f;         ///< Max allowable undertemp fault threshold

// Profile Step Limits
inline constexpr float MIN_RAMP_RATE = 0.1f;     ///< Min ramp rate (°C/s)
inline constexpr float MAX_RAMP_RATE = 10.0f;    ///< Max ramp rate (°C/s)
inline constexpr uint32_t MAX_STEP_TIME_S = 600; ///< Max step hold duration (seconds)

// Watchdog Thresholds
inline constexpr float MIN_STUCK_SSR_RISE = 1.0f;      ///< Min rise threshold for stuck SSR (°C)
inline constexpr float MAX_STUCK_SSR_RISE = 50.0f;     ///< Max rise threshold for stuck SSR (°C)
inline constexpr uint32_t MIN_STUCK_SSR_SEC = 3;       ///< Min window for stuck SSR (s)
inline constexpr uint32_t MAX_STUCK_SSR_SEC = 120;     ///< Max window for stuck SSR (s)

inline constexpr float MIN_NO_RISE_THRESH = 0.5f;      ///< Min rise threshold for heater no-rise (°C)
inline constexpr float MAX_NO_RISE_THRESH = 50.0f;     ///< Max rise threshold for heater no-rise (°C)
inline constexpr uint32_t MIN_NO_RISE_TIMEOUT_S = 5;   ///< Min timeout for heater no-rise (s)
inline constexpr uint32_t MAX_NO_RISE_TIMEOUT_S = 120; ///< Max timeout for heater no-rise (s)

// FSM Hold Gate & Settle Timings
inline constexpr float MIN_HOLD_TOLERANCE = 0.5f;  ///< Min lower/upper hold tolerance (°C)
inline constexpr float MAX_HOLD_TOLERANCE = 30.0f; ///< Max lower/upper hold tolerance (°C)
inline constexpr uint32_t MIN_SETTLE_S = 1;        ///< Min settle duration (s)
inline constexpr uint32_t MAX_SETTLE_S = 60;       ///< Max settle duration (s)

// Process & Fan Timings
inline constexpr uint32_t MIN_FAN_DELAY_S = 0;      ///< Min delay before fan starts in cooling (s)
inline constexpr uint32_t MAX_FAN_DELAY_S = 300;    ///< Max delay before fan starts in cooling (s)
inline constexpr uint32_t MIN_FAN_DURATION_S = 0;   ///< Min fan runtime in cooling (s, 0 = disabled / natural cooling)
inline constexpr uint32_t MAX_FAN_DURATION_S = 600; ///< Max fan runtime in cooling (s)

// Sensor Configuration (MAX31856)
inline constexpr float MIN_EMA_ALPHA = 0.01f;   ///< Min EMA filter alpha
inline constexpr float MAX_EMA_ALPHA = 1.0f;    ///< Max EMA filter alpha
inline constexpr uint8_t MIN_FAULT_STREAK = 1;  ///< Min consecutive fault streak count
inline constexpr uint8_t MAX_FAULT_STREAK = 20; ///< Max consecutive fault streak count
inline constexpr float MIN_CJ_OFFSET = -8.0f;   ///< Min cold-junction offset (°C, MAX31856 CJTO hardware limit)
inline constexpr float MAX_CJ_OFFSET = 7.9f;    ///< Max cold-junction offset (°C, MAX31856 CJTO hardware limit)

// SSR Burst-Fire Windows (ms)
inline constexpr uint32_t MIN_BURST_WINDOW_MS = 500;  ///< Min SSR burst window (ms)
inline constexpr uint32_t MAX_BURST_WINDOW_MS = 4000; ///< Max SSR burst window (ms)

// PID Gains (Global Defaults)
inline constexpr float MIN_PID_KP = 0.0f;   ///< Min Kp
inline constexpr float MAX_PID_KP = 100.0f; ///< Max Kp
inline constexpr float MIN_PID_KI = 0.0f;   ///< Min Ki
inline constexpr float MAX_PID_KI = 10.0f;  ///< Max Ki
inline constexpr float MIN_PID_KD = 0.0f;   ///< Min Kd
inline constexpr float MAX_PID_KD = 100.0f; ///< Max Kd
} // namespace Limits

/**
 * @namespace config::Resolution
 * @brief Single Source of Truth for input step resolutions & decimal precision across the system.
 */
namespace Resolution {
inline constexpr float TEMPERATURE = 0.1f;    ///< Temperature step resolution (°C)
inline constexpr float RAMP_RATE = 0.01f;     ///< Ramp rate step resolution (°C/s)
inline constexpr float PID_GAIN_KP = 0.01f;   ///< PID Kp step resolution
inline constexpr float PID_GAIN_KI = 0.001f;  ///< PID Ki step resolution
inline constexpr float PID_GAIN_KD = 0.01f;   ///< PID Kd step resolution
inline constexpr float FILTER_ALPHA = 0.001f; ///< EMA Filter Alpha step resolution
inline constexpr float OFFSET_TEMP = 0.1f;    ///< Sensor Offset step resolution (°C)
inline constexpr float TOLERANCE_TEMP = 0.1f; ///< Tolerance step resolution (°C)
inline constexpr uint32_t TIME_SEC = 1;       ///< Time step resolution (seconds)
inline constexpr uint32_t TIME_MS = 100;      ///< Time step resolution (milliseconds)
} // namespace Resolution

/**
 * @namespace config::Timing
 * @brief Single Source of Truth for system task execution frequencies and synchronous loop periods.
 *
 * Synchronizes the MAX31856 ADC 4-sample continuous conversion period (~160 ms),
 * the FreeRTOS Core 1 control_task execution cycle (200 ms / 5 Hz), the QuickPID
 * sample time scaling, FSM step runner integration, and thermal simulation step size.
 */
namespace Timing {
inline constexpr uint32_t CONTROL_LOOP_PERIOD_MS = 200; ///< Main Core 1 control loop period (200 ms / 5 Hz)
}

/**
 * @namespace config::Schema
 * @brief Schema versioning identifiers for serialized JSON configurations.
 */
namespace Schema {
inline constexpr uint16_t MACHINE_SETTINGS = 1; ///< Schema version for machine settings configuration
inline constexpr uint16_t REFLOW_PROFILE = 1;   ///< Schema version for reflow profiles
inline constexpr uint16_t PID_LIBRARY = 1;      ///< Schema version for PID gain libraries
} // namespace Schema

/**
 * @namespace config::Buzzer
 * @brief Acoustic signal durations and timings for process notifications.
 */
namespace Buzzer {
inline constexpr uint32_t PREHEAT_DONE_BEEP_MS = 1000; ///< Duration of buzzer beep on preheat completion (ms)
inline constexpr uint32_t REFLOW_DONE_BEEP_MS = 3000;  ///< Duration of buzzer beep on reflow completion (ms)
} // namespace Buzzer

/**
 * @brief Standardized result structure for domain model validation.
 */
struct ValidationResult {
    bool valid = true;                  ///< True if validation passed without errors
    const char* errorMessage = nullptr; ///< Description of validation failure, or nullptr if valid
};

/**
 * @brief Represents a single step in a dual-channel heating profile.
 * Maps directly to fsm::ProfileStep – used by FSM and StorageManager.
 */
struct ProfileStep {
    float temp = 100.0f; ///< Target temperature (°C)
    uint32_t time = 10;  ///< Hold duration (seconds)
    float ramp = 1.0f;   ///< Ramp rate (°C/s)

    /**
     * @brief Validates step parameters against central system limits.
     * @return ValidationResult indicating validation status.
     */
    [[nodiscard]] ValidationResult validate() const;
};

/**
 * @brief Complete dual-channel reflow profile. Top and Bottom channels independent.
 * Maps directly to fsm::ReflowProfile – serialized to *.json via StorageManager.
 */
struct ReflowProfile {
    std::string name{};                     ///< Profile display name (1..30 chars)
    std::string file{};                     ///< Storage filename without path (max 40 chars)
    std::vector<ProfileStep> stepsTop{};    ///< Heating steps for Top heater channel
    std::vector<ProfileStep> stepsBottom{}; ///< Heating steps for Bottom pre-heater channel

    /**
     * @brief Validates profile integrity, name length, filename, and step constraints.
     * @return ValidationResult indicating validation status.
     */
    [[nodiscard]] ValidationResult validate() const;
};

/**
 * @brief Temperature-dependent PID tuning point for gain scheduling interpolation.
 * Used by PID Library tab – interpolated at runtime by pid::PIDController.
 */
struct PidPoint {
    float temp = 100.0f; ///< Temperature setpoint (°C)
    float kp = 2.0f;     ///< Proportional gain (Kp)
    float ki = 0.05f;    ///< Integral gain (Ki)
    float kd = 1.0f;     ///< Derivative gain (Kd)

    /**
     * @brief Validates PID point parameters against central bounds.
     * @return ValidationResult indicating validation status.
     */
    [[nodiscard]] ValidationResult validate() const;
};

/**
 * @brief Complete PID gain scheduling library (Top & Bottom channels).
 * Serialized to /littlefs/config/pid_library.json via StorageManager.
 */
struct PidLibrary {
    std::vector<PidPoint> top{};    ///< PID tuning points for Top heater channel
    std::vector<PidPoint> bottom{}; ///< PID tuning points for Bottom pre-heater channel

    /**
     * @brief Validates PID library size, individual tuning points, and strictly monotonic temperature order.
     * @return ValidationResult indicating validation status.
     */
    [[nodiscard]] ValidationResult validate() const;
};

/**
 * @brief Global Machine Settings – single source of truth for all runtime parameters.
 * Serialized to /littlefs/config/settings.json via StorageManager.
 *
 * CROSS-REFERENCED with:
 *   - safety::SafetyConfig     (safety_watchdog.hpp)
 *   - pid::PIDController       (pid_controller.hpp)
 *   - output::BurstFire        (burst_fire.hpp)
 *   - sensor::MAX31856Config   (max31856.hpp)
 *   - input::InputSystemConfig (input_manager.hpp)
 *   - fsm::ReflowFSM           (reflow_fsm.hpp)
 */
struct MachineSettings {
    // ========================================================================
    // SYSTEM & UI PREFERENCES
    // ========================================================================
    bool simulationMode = true;         ///< Simulation mode (no real SPI hardware)
    std::string language = "en";        ///< UI language: "de", "en"
    std::string theme = "light";        ///< UI theme: "light", "dark"
    bool softwareBuzzerEnabled = true;  ///< Web UI software buzzer sound enable
    bool hardwareBuzzerEnabled = false; ///< Hardware buzzer enable
    std::string defaultProfile{};       ///< Profile loaded on boot (empty = none)

    // UI & Chart Display Preferences
    bool showZones = true;       ///< Show temperature zone bands
    bool showTalLine = true;     ///< Show TAL line at 217°C
    bool showStepMarkers = true; ///< Show step markers on chart
    bool showPidGains = false;   ///< Show active PID gains in HUD badges

    // ========================================================================
    // THERMAL SAFETY LIMITS
    // Consumed by: safety::SafetyConfig
    // ========================================================================
    float maxTempTop = 280.0f;     ///< Absolute max temperature Top (°C)
    float maxTempBottom = 280.0f;  ///< Absolute max temperature Bottom (°C)
    float minTempTop = 5.0f;       ///< Absolute min temperature Top (°C)
    float minTempBottom = 5.0f;    ///< Absolute min temperature Bottom (°C)
    float coolingSafeTemp = 45.0f; ///< Safe to touch temperature after cooling (°C)

    // Master Safety Watchdog (Allows bypassing thermal protection for testbench / tuning)
    bool enableSafetyWatchdog = true; ///< Master enable for SafetyWatchdog (false = bypassed for testbench)

    // Stuck SSR Watchdog
    bool enableStuckSsrCheck = true;    ///< Enable stuck SSR safety watchdog
    float stuckSsrRiseThreshold = 5.0f; ///< °C rise at 0% power = stuck SSR
    uint32_t stuckSsrWindowSec = 10;    ///< Evaluation window in seconds

    // Heater No-Rise Watchdog
    bool enableNoRiseCheck = true;  ///< Enable heater no-rise safety watchdog
    float noRiseThreshold = 3.0f;   ///< Minimum °C rise at 100% power
    uint32_t noRiseTimeoutSec = 15; ///< Timeout for no-rise detection (seconds)

    // ========================================================================
    // FSM HOLD GATE & SETTLE TOLERANCES
    // Consumed by: fsm::ReflowFSM
    // ========================================================================
    float holdLowTolerance = 3.0f;  ///< °C lower tolerance during hold phase
    float holdHighTolerance = 5.0f; ///< °C upper tolerance during hold phase
    uint32_t settleTimeS = 5;       ///< Stability settle duration before hold timer starts (seconds)

    // ========================================================================
    // PROCESS TIMINGS & FAN
    // Consumed by: fsm::ReflowFSM
    // ========================================================================
    uint32_t fanCoolingDelayS = 0;    ///< Delay before fan activates in cooling phase (seconds)
    uint32_t fanCoolingDurationS = 0; ///< Fan runtime during cooling (seconds)

    // ========================================================================
    // SENSOR CONFIGURATION (MAX31856)
    // Consumed by: sensor::MAX31856Config
    // ========================================================================
    bool emaFilterEnabled = true; ///< Enable exponential moving average filter
    float emaAlpha = 0.3f;        ///< EMA smoothing factor (0.01 – 1.0)
    uint8_t faultStreakLimit = 3; ///< Consecutive fault reads before sensor error

    // Cold Junction Calibration Offsets
    float topCjOffset = 0.0f;    ///< Top MAX31856 CJTO calibration offset (°C)
    float bottomCjOffset = 0.0f; ///< Bottom MAX31856 CJTO calibration offset (°C)

    // ========================================================================
    // SSR BURST-FIRE WINDOWS
    // Consumed by: output::BurstFire
    // ========================================================================
    uint32_t topBurstWindowMs = 1000;    ///< Top SSR Burst-Fire time window (ms)
    uint32_t bottomBurstWindowMs = 1000; ///< Bottom SSR Burst-Fire time window (ms)

    // ========================================================================
    // PID PARAMETERS
    // Consumed by: pid::PIDController
    // ========================================================================
    bool pidLibraryEnabled = false; ///< Enable PID gain scheduling library
    float topKp = 2.0f;             ///< Top heater default Kp gain
    float topKi = 0.05f;            ///< Top heater default Ki gain
    float topKd = 1.0f;             ///< Top heater default Kd gain
    float bottomKp = 2.0f;          ///< Bottom heater default Kp gain
    float bottomKi = 0.04f;         ///< Bottom heater default Ki gain
    float bottomKd = 1.0f;          ///< Bottom heater default Kd gain

    /**
     * @brief Validates all machine settings against central system limits.
     * @return ValidationResult indicating validation status and first violation reason if any.
     */
    [[nodiscard]] ValidationResult validate() const;
};

} // namespace config
