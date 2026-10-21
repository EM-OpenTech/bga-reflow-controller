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
 * @file thermal_simulator.hpp
 * @brief Thermal Physics Simulator for BGA Reflow Controller.
 *
 * Simulates Top and Bottom heater thermal physics, heat capacity, dissipation
 * to ambient, and forced convection cooling by the fan.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "config/machine_config.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sim {

/**
 * @class ThermalSimulator
 * @brief Thermal Physics Simulator for BGA Reflow Controller.
 *
 * Simulates Top and Bottom heater physics, thermal mass, heat dissipation
 * to ambient (25°C), and active cooling by the fan.
 */
// ============================================================================
// Thermal Physics Simulator Engine
// ============================================================================

class ThermalSimulator {
public:
    static constexpr float AMBIENT_TEMP = 25.0f; ///< Ambient room temperature baseline (°C)

    /**
     * @brief Default constructor.
     */
    ThermalSimulator() = default;

    /**
     * @brief Sanitizes heating power inputs against NaN, Inf, and range limits.
     * @param power Power percentage input.
     * @return Clamped power between 0.0% and 100.0%.
     */
    static float sanitizePower(float power);

    /**
     * @brief Sanitizes temperature values against NaN, Inf, and sub-ambient cooling.
     * @param temp Temperature in °C.
     * @return Validated temperature, floored at AMBIENT_TEMP.
     */
    static float sanitizeTemperature(float temp);

    /**
     * @brief Resets simulated heater temperatures to an initial temperature.
     * @param initialTemp Starting temperature (°C), defaults to AMBIENT_TEMP (25°C).
     */
    void reset(float initialTemp = AMBIENT_TEMP);

    /**
     * @brief Step the thermal simulation by dtMs milliseconds.
     *
     * @param topPower    Current Top Heater PID power (0.0 - 100.0%)
     * @param bottomPower Current Bottom Heater PID power (0.0 - 100.0%)
     * @param fanActive   True if cooling fan is running
     * @param dtMs        Delta time in milliseconds (e.g. 200ms)
     */
    void update(float topPower, float bottomPower, bool fanActive,
                uint32_t dtMs = config::Timing::CONTROL_LOOP_PERIOD_MS);

    /**
     * @brief Returns current simulated Top Heater temperature.
     * @return Temperature in °C.
     */
    [[nodiscard]] float getTopTemperature() const { return _topTemp; }

    /**
     * @brief Returns current simulated Bottom Heater temperature.
     * @return Temperature in °C.
     */
    [[nodiscard]] float getBottomTemperature() const { return _bottomTemp; }

    /**
     * @brief Sets simulated Top Heater temperature.
     * @param temp Temperature in °C.
     */
    void setTopTemperature(float temp);

    /**
     * @brief Sets simulated Bottom Heater temperature.
     * @param temp Temperature in °C.
     */
    void setBottomTemperature(float temp);

private:
    float _topTemp = AMBIENT_TEMP;    ///< Simulated top heater thermocouple temperature (°C)
    float _bottomTemp = AMBIENT_TEMP; ///< Simulated bottom heater thermocouple temperature (°C)
};

} // namespace sim
