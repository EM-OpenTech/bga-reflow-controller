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
#include <cmath>

namespace output {

// ============================================================================
// Lifecycle & Initialization
// ============================================================================

BurstFire::BurstFire(uint32_t windowMs) : _windowMs(std::clamp(windowMs, MIN_BURST_WINDOW_MS, MAX_BURST_WINDOW_MS)) {}

void BurstFire::begin(uint32_t windowMs) {
    setWindowMs(windowMs);
    _windowStartUs = static_cast<uint64_t>(esp_timer_get_time());
    _requestedPower = 0.0f;
    _activePower = 0.0f;
    _state = false;
    _pendingLatch = true;
}

void BurstFire::setPower(float percent) {
    // 1. Guard against non-finite floating point inputs (NaN, +/-Inf)
    if (!std::isfinite(percent)) {
        _requestedPower = 0.0f;
        _activePower = 0.0f;
        _state = false;
        return;
    }

    _requestedPower = std::clamp(percent, 0.0f, 100.0f);

    // 2. Fail-safe immediate shutoff for zero or sub-threshold power requests
    // Pulses shorter than one full AC cycle (20 ms / MIN_BURST_PULSE_US) are suppressed
    // to prevent DC net offset on zero-crossing SSRs and eliminate relay chatter.
    const uint64_t windowUs = static_cast<uint64_t>(_windowMs) * 1000ULL;
    const auto onTimeUs = static_cast<uint64_t>((_requestedPower / 100.0f) * static_cast<float>(windowUs));
    if (_requestedPower <= 0.0f || onTimeUs < MIN_BURST_PULSE_US) {
        _requestedPower = 0.0f;
        _activePower = 0.0f;
        _state = false;
        return;
    }

    // 3. Initial burst latch before first window tick
    if (_pendingLatch) {
        _activePower = _requestedPower;
    }
}

void BurstFire::setWindowMs(uint32_t windowMs) {
    uint32_t clamped = std::clamp(windowMs, MIN_BURST_WINDOW_MS, MAX_BURST_WINDOW_MS);
    if (_windowMs != clamped) {
        _windowMs = clamped;
        // Re-sync window start timestamp only on actual window duration change
        _windowStartUs = static_cast<uint64_t>(esp_timer_get_time());
        _pendingLatch = true;
    }
}

// ============================================================================
// Time-Proportional PWM Modulation Loop
// ============================================================================

void BurstFire::update() {
    const auto nowUs = static_cast<uint64_t>(esp_timer_get_time());
    const uint64_t windowUs = static_cast<uint64_t>(_windowMs) * 1000ULL;
    if (windowUs == 0) {
        _state = false;
        return;
    }

    _windowStartUs = std::min(_windowStartUs, nowUs);
    uint64_t elapsedUs = nowUs - _windowStartUs;

    // 1. Advance window start timestamp on rollover
    if (elapsedUs >= windowUs) {
        const uint64_t windowsPassed = elapsedUs / windowUs;
        _windowStartUs += (windowsPassed * windowUs);
        elapsedUs %= windowUs;
        _pendingLatch = true;
    }

    // 2. Synchronous window latch: apply requested power at window boundary
    if (_pendingLatch) {
        _activePower = _requestedPower;
        _pendingLatch = false;
    }

    // 3. Early guard: zero power or sub-threshold pulse de-energizes output immediately
    const auto onTimeUs = static_cast<uint64_t>((_activePower / 100.0f) * static_cast<float>(windowUs));
    if (_activePower <= 0.0f || onTimeUs < MIN_BURST_PULSE_US) {
        _state = false;
        return;
    }

    // 4. Time-proportional gate comparison (Happy Path at root level)
    _state = (elapsedUs < onTimeUs);
}

// ============================================================================
// Timing Getters & Controller Reset
// ============================================================================

uint32_t BurstFire::getOnTimeMs() const {
    const float effPower = (_activePower > 0.0f) ? _activePower : _requestedPower;
    const uint64_t windowUs = static_cast<uint64_t>(_windowMs) * 1000ULL;
    const auto onTimeUs = static_cast<uint64_t>((effPower / 100.0f) * static_cast<float>(windowUs));
    if (onTimeUs < MIN_BURST_PULSE_US) {
        return 0;
    }
    return static_cast<uint32_t>(onTimeUs / 1000ULL);
}

uint32_t BurstFire::getOffTimeMs() const {
    return _windowMs - getOnTimeMs();
}

void BurstFire::reset() {
    _requestedPower = 0.0f;
    _activePower = 0.0f;
    _state = false;
    _windowStartUs = static_cast<uint64_t>(esp_timer_get_time());
    _pendingLatch = true;
}

} // namespace output
