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
 * @file thermal_simulator.cpp
 * @brief Implementation of the Thermal Physics Simulator for BGA Reflow Controller.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "simulation/thermal_simulator.hpp"

namespace sim {

float ThermalSimulator::sanitizePower(float power) {
    if (!std::isfinite(power)) {
        return 0.0f;
    }
    return std::clamp(power, 0.0f, 100.0f);
}

float ThermalSimulator::sanitizeTemperature(float temp) {
    if (!std::isfinite(temp) || (temp < AMBIENT_TEMP)) {
        return AMBIENT_TEMP;
    }
    return temp;
}

void ThermalSimulator::reset(float initialTemp) {
    const float safeTemp = sanitizeTemperature(initialTemp);
    _topTemp = safeTemp;
    _bottomTemp = safeTemp;
}

void ThermalSimulator::update(float topPower, float bottomPower, bool fanActive, uint32_t dtMs) {
    if (dtMs == 0) {
        return;
    }

    // Limit maximum Euler integration step to 10 seconds to maintain numerical stability
    constexpr uint32_t MAX_SIM_STEP_MS = 10000;
    const uint32_t clampedDtMs = std::min(dtMs, MAX_SIM_STEP_MS);
    const float dtSec = static_cast<float>(clampedDtMs) / 1000.0f;

    const float safeTopPower = sanitizePower(topPower);
    const float safeBotPower = sanitizePower(bottomPower);

    // Self-heal temperature state if corrupted
    _topTemp = sanitizeTemperature(_topTemp);
    _bottomTemp = sanitizeTemperature(_bottomTemp);

    // 1. Top Heater Thermal Model (~3.5°C/s at 100% power)
    const float topRise = safeTopPower * 0.035f;
    const float topLoss = (_topTemp - AMBIENT_TEMP) * 0.005f;
    const float topFanCooling = fanActive ? ((_topTemp - AMBIENT_TEMP) * 0.02f) : 0.0f;
    _topTemp = std::max(AMBIENT_TEMP, _topTemp + ((topRise - topLoss - topFanCooling) * dtSec));

    // 2. Bottom Heater Thermal Model (Larger thermal mass: ~1.5°C/s at 100% power)
    const float botRise = safeBotPower * 0.015f;
    const float botLoss = (_bottomTemp - AMBIENT_TEMP) * 0.002f;
    const float botFanCooling = fanActive ? ((_bottomTemp - AMBIENT_TEMP) * 0.005f) : 0.0f;
    _bottomTemp = std::max(AMBIENT_TEMP, _bottomTemp + ((botRise - botLoss - botFanCooling) * dtSec));
}

void ThermalSimulator::setTopTemperature(float temp) {
    if (!std::isfinite(temp)) {
        return;
    }
    _topTemp = sanitizeTemperature(temp);
}

void ThermalSimulator::setBottomTemperature(float temp) {
    if (!std::isfinite(temp)) {
        return;
    }
    _bottomTemp = sanitizeTemperature(temp);
}

} // namespace sim
