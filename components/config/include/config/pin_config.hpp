/**
 * @file pin_config.hpp
 * @brief Hardware GPIO Pin Definitions for ESP32 / ESP32-S3 Reflow Controller.
 *
 * @author ESP-IDF Reflow Controller Team
 * @date 2026-09-23
 */

#pragma once

#include "driver/gpio.h"

namespace config {

/**
 * @struct PinConfig
 * @brief Hardware Pin Definitions for ESP32 / ESP32-S3.
 */
struct PinConfig {
    // SPI Bus for MAX31856 Thermocouples
    static constexpr gpio_num_t SPI_SCK   = GPIO_NUM_12; ///< SPI Clock
    static constexpr gpio_num_t SPI_MISO  = GPIO_NUM_13; ///< SPI Master-In-Slave-Out
    static constexpr gpio_num_t SPI_MOSI  = GPIO_NUM_11; ///< SPI Master-Out-Slave-In
    static constexpr gpio_num_t CS_TOP    = GPIO_NUM_10; ///< Chip Select Top MAX31856
    static constexpr gpio_num_t CS_BOTTOM = GPIO_NUM_9;  ///< Chip Select Bottom MAX31856

    // Solid State Relays (24V DC SSR Outputs)
    static constexpr gpio_num_t SSR_TOP    = GPIO_NUM_4; ///< SSR Top Heater Control
    static constexpr gpio_num_t SSR_BOTTOM = GPIO_NUM_5; ///< SSR Bottom Heater Control

    // Auxiliary Outputs
    static constexpr gpio_num_t FAN    = GPIO_NUM_6;  ///< Cooling Fan 24V Auxiliary Output
    static constexpr gpio_num_t LAMP   = GPIO_NUM_7;  ///< Inspection Lamp 24V Auxiliary Output
    static constexpr gpio_num_t BUZZER = GPIO_NUM_15; ///< Acoustic Signal Buzzer Output

    // Physical Input Push-Buttons (Momentary Tactile Switches)
    static constexpr gpio_num_t BTN_START = GPIO_NUM_1; ///< Front-Panel Start Push-Button
    static constexpr gpio_num_t BTN_STOP  = GPIO_NUM_2; ///< Front-Panel Stop / Emergency Push-Button

    // Physical Input Toggle Switches (Latching Contact Switches)
    static constexpr gpio_num_t SW_FAN  = GPIO_NUM_3; ///< Manual Fan Override Toggle Switch
    static constexpr gpio_num_t SW_LAMP = GPIO_NUM_8; ///< Manual Lamp Override Toggle Switch
};

} // namespace config
