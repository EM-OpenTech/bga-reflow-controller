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
 * @file input_manager.hpp
 * @brief Hardware input manager for physical buttons and toggle switches.
 *
 * Handles physical input signals for front-panel momentary push-buttons
 * (Start, Stop/E-Stop) and latching toggle switches (Fan, Lamp) with software debouncing.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include <cstdint>
#include <functional>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "config/pin_config.hpp"
#include "config/machine_config.hpp"

namespace input {

// ============================================================================
// GLOBAL DEFAULT CONSTANTS
// ============================================================================

/// Default software debounce window duration in milliseconds (30 ms)
constexpr uint32_t DEFAULT_DEBOUNCE_MS = 30;

/**
 * @brief Configuration structure for a single physical GPIO input channel.
 * Supports active-LOW / active-HIGH logic, internal pull-up/pull-down, and software debouncing.
 * Used for both momentary push-buttons and latching toggle switches (Schließerkontakte).
 */
struct InputChannelConfig {
    gpio_num_t pin              = GPIO_NUM_NC;           ///< ESP32 GPIO pin number
    bool activeLow              = true;                  ///< true = LOW (0V) is Active/Closed (Active-LOW with GND switch)
    gpio_pullup_t pullUp        = GPIO_PULLUP_ENABLE;    ///< Enable internal pull-up (holds 3.3V when switch open)
    gpio_pulldown_t pullDown    = GPIO_PULLDOWN_DISABLE; ///< Enable internal pull-down if needed
    uint32_t debounceMs         = DEFAULT_DEBOUNCE_MS;   ///< Debounce time window in milliseconds (default: 30 ms)
};

/**
 * @brief Complete Input Hardware Mapping Configuration for Reflow Station.
 *
 * CROSS-REFERENCED with:
 *   - config::PinConfig        (pin_config.hpp)      -> Physical Pin Allocations
 *   - main::AppController      (app_controller.hpp)  -> Event Dispatch Handlers
 */
struct InputSystemConfig {
    // ========================================================================
    // MOMENTARY PUSH-BUTTONS (FRONT-PANEL ACTIONS)
    // ========================================================================
    InputChannelConfig btnStart = { config::PinConfig::BTN_START, true, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, DEFAULT_DEBOUNCE_MS }; ///< Start push-button
    InputChannelConfig btnStop  = { config::PinConfig::BTN_STOP,  true, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, DEFAULT_DEBOUNCE_MS }; ///< Stop / E-Stop push-button

    // ========================================================================
    // LATCHING TOGGLE SWITCHES (PERMANENT MANUAL OVERRIDES)
    // ========================================================================
    InputChannelConfig swFan    = { config::PinConfig::SW_FAN,    true, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, DEFAULT_DEBOUNCE_MS }; ///< Fan toggle switch
    InputChannelConfig swLamp   = { config::PinConfig::SW_LAMP,   true, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, DEFAULT_DEBOUNCE_MS }; ///< Lamp toggle switch
};

/**
 * @brief Internal debouncing state tracker for a single input (button or switch).
 */
struct InputStateTracker {
    bool rawState               = false; ///< Raw instantaneous reading
    bool debouncedState         = false; ///< Stable debounced state (true = Active/Closed)
    bool wasPressedEvent        = false; ///< One-shot click event trigger (for buttons)
    uint64_t lastStateChangeUs  = 0;     ///< Last state toggle timestamp (microseconds)
};

/**
 * @brief Callback function type for momentary button press events.
 */
using ButtonCallback = std::function<void()>;

/**
 * @brief Callback function type for latching toggle switch state change events.
 * @param active true if switch is closed (ON), false if switch is open (OFF)
 */
using SwitchCallback = std::function<void(bool active)>;

/**
 * @class InputManager
 * @brief Manages physical front-panel buttons and manual override switches with software debouncing.
 * 
 * Manages all physical input signals:
 *   - Momentary Push Buttons (Start, Stop/E-Stop).
 *   - Latching Toggle Switches / Schließerkontakte (Manual Fan ON, Manual Lamp ON).
 * 
 * Features:
 *   - Configurable Active-LOW vs. Active-HIGH logic per input channel.
 *   - Internal Pull-Up/Pull-Down configuration via ESP-IDF GPIO driver API.
 *   - Non-blocking microsecond software debouncing using esp_timer_get_time().
 *   - Momentary click callbacks & latching toggle state change callbacks.
 */
class InputManager {
public:
    /**
     * @brief Construct an InputManager instance with optional custom input configuration.
     * @param sysConfig Hardware input pin and debounce configuration.
     */
    explicit InputManager(const InputSystemConfig& sysConfig = InputSystemConfig());

    /**
     * @brief Initialize all GPIO input pins using official ESP-IDF driver/gpio.h API.
     * @return true if all GPIO pins were configured successfully.
     */
    bool begin();

    /**
     * @brief Poll and update debouncing state for all physical inputs (buttons and toggle switches).
     * Must be called periodically in the input / main task loop (e.g. 50 Hz / 20 ms).
     */
    void update();

    // ========================================================================
    // MOMENTARY PUSH-BUTTON CHECKERS & CALLBACKS
    // ========================================================================

    /**
     * @brief Check if the Start button was clicked since the last check (consume-on-read).
     * @return true if button press occurred, false otherwise.
     */
    bool wasStartPressed();

    /**
     * @brief Check if the Stop button was clicked since the last check (consume-on-read).
     * @return true if button press occurred, false otherwise.
     */
    bool wasStopPressed();

    /**
     * @brief Check if Start button is currently physically held down.
     * @return true if button is currently in active/pressed state.
     */
    bool isStartHeld() const { return _startTracker.debouncedState; }

    /**
     * @brief Check if Stop button is currently physically held down.
     * @return true if button is currently in active/pressed state.
     */
    bool isStopHeld() const  { return _stopTracker.debouncedState; }

    /**
     * @brief Register callback function for Start button press event.
     * @param cb Callback function to execute on press.
     */
    void setOnStartPressed(ButtonCallback cb) { _onStartCb = cb; }

    /**
     * @brief Register callback function for Stop button press event.
     * @param cb Callback function to execute on press.
     */
    void setOnStopPressed(ButtonCallback cb)  { _onStopCb  = cb; }

    // ========================================================================
    // LATCHING TOGGLE SWITCHES / SCHLIEßERKONTAKTE (MANUAL FAN & LAMP)
    // ========================================================================

    /**
     * @brief Check current debounced state of physical Fan Toggle Switch.
     * @return true if switch is toggled ON (closed contact), false if OFF (open contact).
     */
    bool isFanSwitchActive() const { return _swFanTracker.debouncedState; }

    /**
     * @brief Check current debounced state of physical Lamp Toggle Switch.
     * @return true if switch is toggled ON (closed contact), false if OFF (open contact).
     */
    bool isLampSwitchActive() const { return _swLampTracker.debouncedState; }

    /**
     * @brief Register callback for Fan Toggle Switch state changes.
     * Triggered whenever the physical switch is toggled ON or OFF.
     * @param cb Callback function receiving new boolean state.
     */
    void setOnFanSwitchChanged(SwitchCallback cb) { _onFanSwitchCb = cb; }

    /**
     * @brief Register callback for Lamp Toggle Switch state changes.
     * Triggered whenever the physical switch is toggled ON or OFF.
     * @param cb Callback function receiving new boolean state.
     */
    void setOnLampSwitchChanged(SwitchCallback cb) { _onLampSwitchCb = cb; }

private:
    InputSystemConfig  _sysConfig;       ///< Hardware pin and debounce parameters

    InputStateTracker  _startTracker;    ///< Debounce state for Start push-button
    InputStateTracker  _stopTracker;     ///< Debounce state for Stop push-button
    InputStateTracker  _swFanTracker;    ///< Debounce state for Fan toggle switch
    InputStateTracker  _swLampTracker;   ///< Debounce state for Lamp toggle switch

    ButtonCallback     _onStartCb      = nullptr; ///< Callback invoked on Start button press
    ButtonCallback     _onStopCb       = nullptr; ///< Callback invoked on Stop button press
    SwitchCallback     _onFanSwitchCb  = nullptr; ///< Callback invoked on Fan switch state toggle
    SwitchCallback     _onLampSwitchCb = nullptr; ///< Callback invoked on Lamp switch state toggle

    /**
     * @brief Configures a single GPIO input pin.
     * @param chConfig Pin and pull-up/down settings.
     * @return true if configuration succeeded.
     */
    bool initChannel(const InputChannelConfig& chConfig);

    /**
     * @brief Debounces and processes a momentary push-button input.
     * @param chConfig Channel configuration.
     * @param tracker State tracker instance for this button.
     * @param cb Press event callback function.
     */
    void updateButton(const InputChannelConfig& chConfig, InputStateTracker& tracker, ButtonCallback cb);

    /**
     * @brief Debounces and processes a latching toggle switch input.
     * @param chConfig Channel configuration.
     * @param tracker State tracker instance for this switch.
     * @param cb Toggle event callback function.
     */
    void updateSwitch(const InputChannelConfig& chConfig, InputStateTracker& tracker, SwitchCallback cb);
};

} // namespace input
