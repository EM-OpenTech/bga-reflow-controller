/**
 * @file input_manager.hpp
 * @brief Native C++ ESP-IDF v6.0.2 Hardware Input Manager.
 *
 * Handles physical input signals for front-panel momentary push-buttons
 * (Start, Stop/E-Stop) and latching toggle switches (Fan, Lamp) with software debouncing.
 *
 * @author ESP-IDF Reflow Controller Team
 * @date 2026-09-23
 */

#pragma once

#include <cstdint>
#include <functional>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "config/pin_config.hpp"

namespace input {

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
    uint32_t debounceMs         = 30;                    ///< Debounce time window in milliseconds (default: 30 ms)
};

/**
 * @brief Complete Input Hardware Mapping Configuration for Reflow Station.
 */
struct InputSystemConfig {
    // Momentary Push-Buttons (Front-Panel Actions)
    InputChannelConfig btnStart = { config::PinConfig::BTN_START, true, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, 30 }; ///< Start push-button
    InputChannelConfig btnStop  = { config::PinConfig::BTN_STOP,  true, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, 30 }; ///< Stop push-button

    // Latching Toggle Switches / Schließerkontakte (Permanent ON/OFF Manual Controls)
    InputChannelConfig swFan    = { config::PinConfig::SW_FAN,    true, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, 30 }; ///< Fan toggle switch
    InputChannelConfig swLamp   = { config::PinConfig::SW_LAMP,   true, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, 30 }; ///< Lamp toggle switch
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
 * @brief Native C++ ESP-IDF v6.0.2 Hardware Input Manager.
 * 
 * Manages all physical input signals:
 *   - Momentary Push Buttons (Start, Stop/E-Stop).
 *   - Latching Toggle Switches / Schließerkontakte (Manual Fan ON, Manual Lamp ON).
 * 
 * Features:
 *   - Configurable Active-LOW vs. Active-HIGH logic per input.
 *   - Internal Pull-Up/Pull-Down configuration via official driver/gpio.h gpio_config_t API.
 *   - Non-blocking microsecond software debouncing using esp_timer_get_time().
 *   - Momentary click callbacks & latching toggle state change callbacks.
 *   - Zero Arduino framework dependencies.
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
    InputSystemConfig  _sysConfig;

    InputStateTracker  _startTracker;
    InputStateTracker  _stopTracker;
    InputStateTracker  _swFanTracker;
    InputStateTracker  _swLampTracker;

    ButtonCallback     _onStartCb      = nullptr;
    ButtonCallback     _onStopCb       = nullptr;
    SwitchCallback     _onFanSwitchCb  = nullptr;
    SwitchCallback     _onLampSwitchCb = nullptr;

    bool initChannel(const InputChannelConfig& chConfig);
    void updateButton(const InputChannelConfig& chConfig, InputStateTracker& tracker, ButtonCallback cb);
    void updateSwitch(const InputChannelConfig& chConfig, InputStateTracker& tracker, SwitchCallback cb);
};

} // namespace input
