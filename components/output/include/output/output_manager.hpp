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
 * @file output_manager.hpp
 * @brief Hardware GPIO output manager for SSRs, cooling fan, lamp, and buzzer.
 *
 * Controls actuator outputs with configurable polarity (Active-HIGH / Active-LOW),
 * internal pull resistors, and safety inhibit hardware lockout.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include <cstdint>
#include "driver/gpio.h"
#include "config/pin_config.hpp"

namespace output {

/**
 * @brief Configuration structure for a single physical GPIO output channel.
 * Allows independent pin selection, active polarity (active-HIGH vs active-LOW inverted logic),
 * and pull-down/pull-up hardware configuration per pin.
 */
struct OutputChannelConfig {
    gpio_num_t pin              = GPIO_NUM_NC;           ///< ESP32 GPIO pin number
    bool activeHigh             = true;                  ///< true = HIGH (1) is ON, false = LOW (0) is ON (inverted logic)
    gpio_pulldown_t pullDown    = GPIO_PULLDOWN_ENABLE;  ///< Enable internal pull-down during boot/reset
    gpio_pullup_t pullUp        = GPIO_PULLUP_DISABLE;   ///< Enable internal pull-up if needed
};

/**
 * @brief Complete Output Hardware Mapping Configuration for Reflow Station.
 *
 * CROSS-REFERENCED with:
 *   - config::PinConfig        (pin_config.hpp)      -> Physical Pin Assignments
 *   - safety::SafetyWatchdog   (safety_watchdog.hpp) -> Hardware Inhibit Gating
 *   - main::AppController      (app_controller.hpp)  -> System Initialization & Task Binding
 */
struct OutputSystemConfig {
    // ============================================================================
    // SOLID STATE RELAYS (HEATING CONTROL)
    // ============================================================================
    OutputChannelConfig ssrTop    = { config::PinConfig::SSR_TOP,    true, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE }; ///< SSR Top heating element output
    OutputChannelConfig ssrBottom = { config::PinConfig::SSR_BOTTOM, true, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE }; ///< SSR Bottom pre-heater output

    // ============================================================================
    // AUXILIARY POWER ACTUATORS & INDICATORS
    // ============================================================================
    OutputChannelConfig fan       = { config::PinConfig::FAN,        true, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE }; ///< Cooling fan 24V auxiliary driver
    OutputChannelConfig lamp      = { config::PinConfig::LAMP,       true, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE }; ///< Inspection light 24V driver
    OutputChannelConfig buzzer    = { config::PinConfig::BUZZER,     true, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE }; ///< Acoustic alarm buzzer output
};

/**
 * @class OutputManager
 * @brief Hardware GPIO output actuator manager for the BGA Reflow Controller.
 * 
 * Controls all physical outputs (SSR Top, SSR Bottom, Fan, Lamp, Buzzer).
 * Features:
 *   - Configurable Active-HIGH vs. Active-LOW logic per channel (supports inverted relay modules).
 *   - Configurable internal pull-down/pull-up resistors via ESP-IDF GPIO driver API.
 *   - Safety Inhibit Lockout: When safety watchdog triggers inhibit, all SSR outputs are forced OFF.
 */
class OutputManager {
public:
    /**
     * @brief Construct an OutputManager instance with optional custom channel mapping.
     * @param sysConfig Pin and polarity configuration for all channels.
     */
    explicit OutputManager(const OutputSystemConfig& sysConfig = OutputSystemConfig());

    /**
     * @brief Initialize all GPIO pins using official ESP-IDF driver/gpio.h API.
     * All pins are safely driven to their OFF (inactive) state during initialization.
     * @return true if all GPIO pins were configured successfully, false otherwise.
     */
    bool begin();

    // ============================================================================
    // SSR CONTROL METHODS (Driven by BurstFire state in control task)
    // ============================================================================

    /**
     * @brief Set Top SSR output state (ON/OFF).
     * @param on Desired logical state (true = ON, false = OFF)
     */
    void setSsrTop(bool on);

    /**
     * @brief Set Bottom SSR output state (ON/OFF).
     * @param on Desired logical state (true = ON, false = OFF)
     */
    void setSsrBottom(bool on);

    // ============================================================================
    // AUXILIARY CONTROL METHODS
    // ============================================================================

    /**
     * @brief Set Cooling Fan output state (ON/OFF).
     * @param on Desired logical state (true = ON, false = OFF)
     */
    void setFan(bool on);

    /**
     * @brief Set Work Light / Lamp output state (ON/OFF).
     * @param on Desired logical state (true = ON, false = OFF)
     */
    void setLamp(bool on);

    /**
     * @brief Set Acoustic Alarm / Buzzer output state (ON/OFF).
     * @param on Desired logical state (true = ON, false = OFF)
     */
    void setBuzzer(bool on);

    // ============================================================================
    // SAFETY INHIBIT LOCKOUT
    // ============================================================================

    /**
     * @brief Set Safety Inhibit flag.
     * When inhibited (true), all SSR outputs are forced OFF regardless of PID requests.
     * @param inhibit true to force SSRs OFF, false to restore normal operation.
     */
    void setInhibit(bool inhibit);

    /**
     * @brief Query active Safety Inhibit status.
     * @return true if safety inhibit is currently active, false otherwise.
     */
    bool isInhibited() const { return _inhibit; }

    // ============================================================================
    // STATE GETTERS
    // ============================================================================

    /**
     * @brief Query active logical state of Top SSR output.
     * @return true if ON (and not inhibited), false otherwise.
     */
    bool getSsrTopState()    const { return _inhibit ? false : _ssrTopState; }

    /**
     * @brief Query active logical state of Bottom SSR output.
     * @return true if ON (and not inhibited), false otherwise.
     */
    bool getSsrBottomState() const { return _inhibit ? false : _ssrBottomState; }

    /**
     * @brief Query active logical state of Cooling Fan output.
     * @return true if ON, false if OFF.
     */
    bool getFanState()       const { return _fanState; }

    /**
     * @brief Query active logical state of Work Light / Lamp output.
     * @return true if ON, false if OFF.
     */
    bool getLampState()      const { return _lampState; }

    /**
     * @brief Query active logical state of Acoustic Alarm / Buzzer output.
     * @return true if ON, false if OFF.
     */
    bool getBuzzerState()    const { return _buzzerState; }

    /**
     * @brief Reconfigure active level polarity (active-HIGH vs active-LOW) for a channel at runtime.
     * @param pin GPIO pin number to reconfigure.
     * @param activeHigh true for active-HIGH logic, false for active-LOW inverted logic.
     */
    void setChannelPolarity(gpio_num_t pin, bool activeHigh);

private:
    OutputSystemConfig _sysConfig;       ///< Hardware pin and polarity mapping

    bool _inhibit        = false;        ///< Safety watchdog inhibit flag (forces SSRs OFF)
    bool _ssrTopState    = false;        ///< Logical command state for Top SSR
    bool _ssrBottomState = false;        ///< Logical command state for Bottom SSR
    bool _fanState       = false;        ///< Physical output state of cooling fan
    bool _lampState      = false;        ///< Physical output state of inspection lamp
    bool _buzzerState    = false;        ///< Physical output state of notification buzzer

    /**
     * @brief Helper to configure a single GPIO pin via gpio_config.
     * @param chConfig Channel configuration structure.
     * @return true on success, false on error.
     */
    bool initChannel(const OutputChannelConfig& chConfig);

    /**
     * @brief Helper to write active/inactive state to physical GPIO pin respecting polarity.
     * @param chConfig Channel configuration structure.
     * @param active true if logical ON, false if logical OFF.
     */
    void writeChannel(const OutputChannelConfig& chConfig, bool active);
};

} // namespace output

