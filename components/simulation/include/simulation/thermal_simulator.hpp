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

#include <cstdint>
#include <cmath>
#include <algorithm>
#include "config/machine_config.hpp"

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
     * @brief Resets simulated heater temperatures to an initial temperature.
     * @param initialTemp Starting temperature (°C), defaults to AMBIENT_TEMP (25°C).
     */
    void reset(float initialTemp = AMBIENT_TEMP) {
        float safeTemp = (std::isnan(initialTemp) || std::isinf(initialTemp) || initialTemp < AMBIENT_TEMP)
                             ? AMBIENT_TEMP
                             : initialTemp;
        _topTemp    = safeTemp;
        _bottomTemp = safeTemp;
    }

    /**
     * @brief Step the thermal simulation by dtMs milliseconds.
     * 
     * @param topPower    Current Top Heater PID power (0.0 - 100.0%)
     * @param bottomPower Current Bottom Heater PID power (0.0 - 100.0%)
     * @param fanActive   True if cooling fan is running
     * @param dtMs        Delta time in milliseconds (e.g. 200ms)
     */
    void update(float topPower, float bottomPower, bool fanActive, uint32_t dtMs = config::Timing::CONTROL_LOOP_PERIOD_MS) {
        float dtSec = static_cast<float>(dtMs) / 1000.0f;

        // Sanitize and clamp power inputs against NaN, Inf, negative, or excessive values
        float safeTopPower = (std::isnan(topPower) || std::isinf(topPower))
                                 ? 0.0f
                                 : std::clamp(topPower, 0.0f, 100.0f);
        float safeBotPower = (std::isnan(bottomPower) || std::isinf(bottomPower))
                                 ? 0.0f
                                 : std::clamp(bottomPower, 0.0f, 100.0f);

        // Self-heal temperature state if corrupted
        if (std::isnan(_topTemp) || std::isinf(_topTemp) || _topTemp < AMBIENT_TEMP) {
            _topTemp = AMBIENT_TEMP;
        }
        if (std::isnan(_bottomTemp) || std::isinf(_bottomTemp) || _bottomTemp < AMBIENT_TEMP) {
            _bottomTemp = AMBIENT_TEMP;
        }

        // ====================================================================
        // Top Heater Thermal Model
        // ====================================================================
        // Rise: ~3.5°C/s at 100% power
        float topRise = safeTopPower * 0.035f;
        // Natural heat dissipation to room temperature
        float topLoss = (_topTemp - AMBIENT_TEMP) * 0.005f;
        // Forced convection by active fan
        float topFanCooling = fanActive ? ((_topTemp - AMBIENT_TEMP) * 0.02f) : 0.0f;

        _topTemp += (topRise - topLoss - topFanCooling) * dtSec;
        if (_topTemp < AMBIENT_TEMP) _topTemp = AMBIENT_TEMP;

        // ====================================================================
        // Bottom Heater Thermal Model (Larger thermal mass)
        // ====================================================================
        // Rise: ~1.5°C/s at 100% power (heavier quartz/ceramic mass)
        float botRise = safeBotPower * 0.015f;
        float botLoss = (_bottomTemp - AMBIENT_TEMP) * 0.002f;
        float botFanCooling = fanActive ? ((_bottomTemp - AMBIENT_TEMP) * 0.005f) : 0.0f;

        _bottomTemp += (botRise - botLoss - botFanCooling) * dtSec;
        if (_bottomTemp < AMBIENT_TEMP) _bottomTemp = AMBIENT_TEMP;
    }

    /**
     * @brief Returns current simulated Top Heater temperature.
     * @return Temperature in °C.
     */
    float getTopTemperature() const { return _topTemp; }

    /**
     * @brief Returns current simulated Bottom Heater temperature.
     * @return Temperature in °C.
     */
    float getBottomTemperature() const { return _bottomTemp; }

    /**
     * @brief Sets simulated Top Heater temperature.
     * @param temp Temperature in °C.
     */
    void setTopTemperature(float temp) {
        if (!std::isnan(temp) && !std::isinf(temp)) {
            _topTemp = std::max(temp, AMBIENT_TEMP);
        }
    }

    /**
     * @brief Sets simulated Bottom Heater temperature.
     * @param temp Temperature in °C.
     */
    void setBottomTemperature(float temp) {
        if (!std::isnan(temp) && !std::isinf(temp)) {
            _bottomTemp = std::max(temp, AMBIENT_TEMP);
        }
    }

private:
    float _topTemp    = AMBIENT_TEMP; ///< Simulated top heater thermocouple temperature (°C)
    float _bottomTemp = AMBIENT_TEMP; ///< Simulated bottom heater thermocouple temperature (°C)
};

} // namespace sim
