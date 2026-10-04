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
 * @file burst_fire.cpp
 * @brief Implementation of time-proportional control (burst-fire PWM) for SSRs.
 *
 * Uses microsecond high-resolution timestamps via esp_timer_get_time() to calculate
 * discrete ON/OFF SSR gate periods without phase drift.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "output/burst_fire.hpp"
#include <algorithm>

namespace output {

// ============================================================================
// Lifecycle & Initialization
// ============================================================================

BurstFire::BurstFire(uint32_t windowMs)
    : _windowMs(std::clamp(windowMs, MIN_BURST_WINDOW_MS, MAX_BURST_WINDOW_MS)),
      _windowStartUs(0),
      _power(0.0f),
      _state(false) {}

void BurstFire::begin(uint32_t windowMs) {
    setWindowMs(windowMs);
    _windowStartUs = static_cast<uint64_t>(esp_timer_get_time());
    _power = 0.0f;
    _state = false;
}

void BurstFire::setPower(float percent) {
    _power = std::clamp(percent, 0.0f, 100.0f);
}

void BurstFire::setWindowMs(uint32_t windowMs) {
    uint32_t clamped = std::clamp(windowMs, MIN_BURST_WINDOW_MS, MAX_BURST_WINDOW_MS);
    if (_windowMs != clamped) {
        _windowMs = clamped;
        // Re-sync window start timestamp only on actual window duration change
        _windowStartUs = static_cast<uint64_t>(esp_timer_get_time());
    }
}

// ============================================================================
// Time-Proportional PWM Modulation Loop
// ============================================================================

void BurstFire::update() {
    uint64_t nowUs    = static_cast<uint64_t>(esp_timer_get_time());
    uint64_t windowUs = static_cast<uint64_t>(_windowMs) * 1000ULL;
    if (windowUs == 0) {
        _state = false;
        return;
    }

    if (nowUs < _windowStartUs) {
        _windowStartUs = nowUs;
    }

    uint64_t elapsedUs = nowUs - _windowStartUs;

    // Advance window start timestamp mathematically to eliminate cumulative timing drift without blocking loops
    if (elapsedUs >= windowUs) {
        uint64_t windowsPassed = elapsedUs / windowUs;
        _windowStartUs += windowsPassed * windowUs;
        elapsedUs %= windowUs;
    }

    uint64_t onTimeUs = static_cast<uint64_t>((_power / 100.0f) * static_cast<float>(windowUs));
    _state = (elapsedUs < onTimeUs);
}

// ============================================================================
// Timing Getters & Controller Reset
// ============================================================================

uint32_t BurstFire::getOnTimeMs() const {
    return static_cast<uint32_t>((_power / 100.0f) * static_cast<float>(_windowMs));
}

uint32_t BurstFire::getOffTimeMs() const {
    return _windowMs - getOnTimeMs();
}

void BurstFire::reset() {
    _power = 0.0f;
    _state = false;
    _windowStartUs = static_cast<uint64_t>(esp_timer_get_time());
}

} // namespace output
