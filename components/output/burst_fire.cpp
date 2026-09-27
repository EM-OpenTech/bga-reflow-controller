/**
 * @file burst_fire.cpp
 * @brief Implementation of time-proportional control (burst-fire PWM) for SSRs.
 * @author BGA Reflow Controller Team
 */

#include "output/burst_fire.hpp"
#include <algorithm>

namespace output {

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
    _windowMs = std::clamp(windowMs, MIN_BURST_WINDOW_MS, MAX_BURST_WINDOW_MS);
    // Re-sync window start timestamp on window duration change
    _windowStartUs = static_cast<uint64_t>(esp_timer_get_time());
}

void BurstFire::update() {
    uint64_t nowUs     = static_cast<uint64_t>(esp_timer_get_time());
    uint64_t windowUs  = static_cast<uint64_t>(_windowMs) * 1000ULL;
    uint64_t elapsedUs = nowUs - _windowStartUs;

    // Advance window start timestamp iteratively to eliminate cumulative timing drift
    while (elapsedUs >= windowUs) {
        _windowStartUs += windowUs;
        elapsedUs      -= windowUs;
    }

    uint64_t onTimeUs = static_cast<uint64_t>((_power / 100.0f) * static_cast<float>(windowUs));
    _state = (elapsedUs < onTimeUs);
}

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
