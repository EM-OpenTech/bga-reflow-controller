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
 * @file pin_config.hpp
 * @brief Hardware GPIO pin mapping, SPI bus assignments, and peripheral routing.
 *
 * Serves as the Single Source of Truth for hardware pin definitions on ESP32-S3,
 * defining all physical pin allocations for thermocouple SPI interfaces, solid state relays (SSR),
 * auxiliary power drivers (cooling fan, inspection lamp, buzzer), and front-panel tactile inputs.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "driver/gpio.h"

namespace config {

/**
 * @struct PinConfig
 * @brief Central hardware GPIO pin definitions for the BGA Reflow Controller.
 *
 * CROSS-REFERENCED with:
 *   - sensor::MAX31856         (max31856.hpp)        -> SPI Bus & Chip Selects
 *   - output::BurstFire        (burst_fire.hpp)      -> SSR Heater Control Outputs
 *   - output::OutputManager    (output_manager.hpp)  -> Fan, Lamp, and Buzzer Actuators
 *   - input::InputManager      (input_manager.hpp)   -> Start/Stop Buttons & Manual Overrides
 */
struct PinConfig {
    // ============================================================================
    // SPI BUS FOR THERMOCOUPLE SENSORS (MAX31856)
    // Consumed by: sensor::MAX31856 / main::AppController
    // ============================================================================
    static constexpr gpio_num_t SPI_SCK   = GPIO_NUM_12; ///< SPI Master Clock
    static constexpr gpio_num_t SPI_MISO  = GPIO_NUM_13; ///< SPI Master-In-Slave-Out (Data from MAX31856)
    static constexpr gpio_num_t SPI_MOSI  = GPIO_NUM_11; ///< SPI Master-Out-Slave-In (Configuration to MAX31856)
    static constexpr gpio_num_t CS_TOP    = GPIO_NUM_10; ///< Active-LOW Chip Select for Top Heater Thermocouple
    static constexpr gpio_num_t CS_BOTTOM = GPIO_NUM_9;  ///< Active-LOW Chip Select for Bottom Heater Thermocouple

    // ============================================================================
    // HEATER CONTROL OUTPUTS (SOLID STATE RELAYS / 24V DC SSR)
    // Consumed by: output::BurstFire / output::OutputManager
    // ============================================================================
    static constexpr gpio_num_t SSR_TOP    = GPIO_NUM_4; ///< Top Heating Element SSR Control Output
    static constexpr gpio_num_t SSR_BOTTOM = GPIO_NUM_5; ///< Bottom Pre-Heater SSR Control Output

    // ============================================================================
    // AUXILIARY & ACTUATOR OUTPUTS
    // Consumed by: output::OutputManager
    // ============================================================================
    static constexpr gpio_num_t FAN    = GPIO_NUM_6;  ///< Forced-Air Cooling Fan 24V Auxiliary Output
    static constexpr gpio_num_t LAMP   = GPIO_NUM_7;  ///< PCB Inspection Lamp 24V Auxiliary Output
    static constexpr gpio_num_t BUZZER = GPIO_NUM_15; ///< Acoustic Notification Buzzer Output

    // ============================================================================
    // PHYSICAL FRONT-PANEL INPUTS (TACTILE MOMENTARY PUSH-BUTTONS)
    // Consumed by: input::InputManager
    // ============================================================================
    static constexpr gpio_num_t BTN_START = GPIO_NUM_1; ///< Front-Panel Start Push-Button (Active-LOW, Pulled-Up)
    static constexpr gpio_num_t BTN_STOP  = GPIO_NUM_2; ///< Front-Panel Stop / Emergency Push-Button (Active-LOW, Pulled-Up)

    // ============================================================================
    // PHYSICAL OVERRIDE INPUTS (LATCHING TOGGLE SWITCHES)
    // Consumed by: input::InputManager
    // ============================================================================
    static constexpr gpio_num_t SW_FAN  = GPIO_NUM_3; ///< Manual Cooling Fan Override Switch (Latching Contact)
    static constexpr gpio_num_t SW_LAMP = GPIO_NUM_8; ///< Manual Inspection Lamp Override Switch (Latching Contact)
};

} // namespace config

