/**
 * @file output_manager.hpp
 * @brief Hardware GPIO output manager for SSRs, fan, lamp, and buzzer.
 * @author BGA Reflow Controller Team
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
 */
struct OutputSystemConfig {
    OutputChannelConfig ssrTop    = { config::PinConfig::SSR_TOP,    true, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE };
    OutputChannelConfig ssrBottom = { config::PinConfig::SSR_BOTTOM, true, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE };
    OutputChannelConfig fan       = { config::PinConfig::FAN,        true, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE };
    OutputChannelConfig lamp      = { config::PinConfig::LAMP,       true, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE };
    OutputChannelConfig buzzer    = { config::PinConfig::BUZZER,     true, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE };
};

/**
 * @brief Native C++ ESP-IDF v6.0.2 Hardware Output Manager.
 * 
 * Controls all physical outputs (SSR Top, SSR Bottom, Fan, Lamp, Buzzer).
 * Features:
 *   - Configurable Active-HIGH vs. Active-LOW logic per channel (supports inverted relay modules).
 *   - Configurable internal pull-down/pull-up resistors via official driver/gpio.h gpio_config_t API.
 *   - Safety Inhibit Lockout: When safety watchdog triggers inhibit, all SSR outputs are forced OFF.
 *   - Zero Arduino dependencies.
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

    // ========================================================================
    // SSR CONTROL METHODS (Driven by BurstFire state in control task)
    // ========================================================================

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

    // ========================================================================
    // AUXILIARY CONTROL METHODS
    // ========================================================================

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

    // ========================================================================
    // SAFETY INHIBIT LOCKOUT
    // ========================================================================

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

    // ========================================================================
    // STATE GETTERS
    // ========================================================================

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
    OutputSystemConfig _sysConfig;

    bool _inhibit        = false;
    bool _ssrTopState    = false;
    bool _ssrBottomState = false;
    bool _fanState       = false;
    bool _lampState      = false;
    bool _buzzerState    = false;

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

