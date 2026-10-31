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
 * @file gain_scheduler.hpp
 * @brief Interpolation engine for temperature-dependent PID gains.
 *
 * Provides linear piece-wise interpolation of Kp, Ki, Kd parameters across
 * temperature support points defined in config::PidLibrary using linear guard
 * clauses and dedicated bracketing interpolation helpers.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "config/machine_config.hpp"
#include <cmath>
#include <cstddef>

namespace pid {

/**
 * @brief Container for a set of PID tuning gains.
 */
struct PidGains {
    float kp = 0.0f;
    float ki = 0.0f;
    float kd = 0.0f;
};

/**
 * @class GainScheduler
 * @brief Static utility for interpolating PID gains between temperature setpoints.
 */
class GainScheduler {
public:
    /**
     * @brief Linearly interpolate between two bracketing support points.
     */
    static PidGains interpolatePair(float targetTemp, const config::PidPoint& p1, const config::PidPoint& p2) {
        if (std::isnan(targetTemp) || std::isinf(targetTemp)) {
            return {p1.kp, p1.ki, p1.kd};
        }
        float deltaT = p2.temp - p1.temp;
        if (deltaT <= 0.001f || std::isnan(deltaT) || std::isinf(deltaT)) {
            return {p1.kp, p1.ki, p1.kd};
        }
        float ratio = (targetTemp - p1.temp) / deltaT;
        return {p1.kp + (ratio * (p2.kp - p1.kp)), p1.ki + (ratio * (p2.ki - p1.ki)),
                p1.kd + (ratio * (p2.kd - p1.kd))};
    }

    /**
     * @brief Interpolate PID gains linearly between neighbouring temperature points.
     *
     * @param targetTemp Target temperature (°C)
     * @param pts Pointer to sorted array of PidPoint support points
     * @param count Number of support points
     * @param fallback Default gains used if library is empty
     * @return Interpolated PidGains
     */
    static PidGains interpolate(float targetTemp, const config::PidPoint* pts, size_t count, const PidGains& fallback) {
        if (pts == nullptr || count == 0 || std::isnan(targetTemp) || std::isinf(targetTemp)) {
            return fallback;
        }

        // Below lowest point: clamp to first point
        if (targetTemp <= pts[0].temp) {
            return {pts[0].kp, pts[0].ki, pts[0].kd};
        }

        // Above highest point: clamp to last point
        if (targetTemp >= pts[count - 1].temp) {
            return {pts[count - 1].kp, pts[count - 1].ki, pts[count - 1].kd};
        }

        // Find bracketing pair and interpolate
        for (size_t i = 0; i < count - 1; ++i) {
            if (targetTemp >= pts[i].temp && targetTemp < pts[i + 1].temp) {
                return interpolatePair(targetTemp, pts[i], pts[i + 1]);
            }
        }

        return fallback;
    }
};

} // namespace pid
