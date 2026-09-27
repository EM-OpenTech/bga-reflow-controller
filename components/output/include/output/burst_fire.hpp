/**
 * @file burst_fire.hpp
 * @brief Time-proportional control (burst-fire PWM) for solid state relays.
 * @author BGA Reflow Controller Team
 */

#pragma once

#include <cstdint>
#include "esp_timer.h"

namespace output {

// ============================================================================
// GLOBAL DEFAULT CONSTANTS
// Easily adjustable default burst window size for SSR Time-Proportional Control
// ============================================================================

/// Default Time-Proportional window duration in milliseconds (1000 ms = 1 second)
constexpr uint32_t DEFAULT_BURST_WINDOW_MS = 1000;

/// Minimum allowed burst window duration in milliseconds (100 ms)
constexpr uint32_t MIN_BURST_WINDOW_MS = 100;

/// Maximum allowed burst window duration in milliseconds (10,000 ms = 10 seconds)
constexpr uint32_t MAX_BURST_WINDOW_MS = 10000;

/**
 * @brief Native C++ Time-Proportional Control (Burst-Fire PWM) for Solid State Relays (SSR).
 * 
 * Converts a 0.0% to 100.0% PID power input into a discrete ON/OFF duty cycle
 * over a configurable time window (Time-Proportional Control).
 * 
 * Designed for zero-crossing SSRs driving ceramic heaters. Uses ESP-IDF high-resolution
 * timer (esp_timer_get_time) with microsecond precision to eliminate phase drift.
 * 
 * Example (Window = 1000 ms, Power = 30.0%):
 *   - SSR ON for 300 ms
 *   - SSR OFF for 700 ms
 * 
 * Independent instances allow Top and Bottom heaters to have separate, custom window sizes.
 */
class BurstFire {
public:
    /**
     * @brief Construct a BurstFire instance with an optional initial window duration.
     * @param windowMs Window duration in milliseconds (defaults to DEFAULT_BURST_WINDOW_MS)
     */
    explicit BurstFire(uint32_t windowMs = DEFAULT_BURST_WINDOW_MS);

    /**
     * @brief Initialize or re-synchronize the time window starting point.
     * @param windowMs Optional window duration in milliseconds
     */
    void begin(uint32_t windowMs = DEFAULT_BURST_WINDOW_MS);

    /**
     * @brief Set requested heating power percentage from PID controller (0.0% to 100.0%).
     * @param percent Power percentage [0.0 - 100.0]
     */
    void setPower(float percent);

    /**
     * @brief Query current requested heating power percentage.
     * @return float Power percentage [0.0 - 100.0]
     */
    float getPower() const { return _power; }

    /**
     * @brief Dynamically set the time window duration in milliseconds (e.g. for Top/Bottom UI settings).
     * @param windowMs New window duration in milliseconds [MIN_BURST_WINDOW_MS - MAX_BURST_WINDOW_MS]
     */
    void setWindowMs(uint32_t windowMs);

    /**
     * @brief Query current time window duration in milliseconds.
     * @return uint32_t Window duration in ms
     */
    uint32_t getWindowMs() const { return _windowMs; }

    /**
     * @brief Calculate current SSR state based on elapsed time within the active window.
     * Must be called regularly in the control loop (e.g., every 10 ms to 100 ms).
     */
    void update();

    /**
     * @brief Query calculated physical SSR output state.
     * @return true SSR should be energized (ON)
     * @return false SSR should be de-energized (OFF)
     */
    bool getState() const { return _state; }

    /**
     * @brief Query active ON duration in milliseconds for the current cycle.
     * @return uint32_t ON duration in ms.
     */
    uint32_t getOnTimeMs() const;

    /**
     * @brief Query active OFF duration in milliseconds for the current cycle.
     * @return uint32_t OFF duration in ms.
     */
    uint32_t getOffTimeMs() const;

    /**
     * @brief Reset window start timestamp and force SSR state to OFF.
     */
    void reset();

private:
    uint32_t _windowMs;       ///< Total time window duration in milliseconds
    uint64_t _windowStartUs;  ///< Hardware start timestamp of current window (esp_timer_get_time in µs)
    float    _power;          ///< Target power percentage [0.0 - 100.0%]
    bool     _state;          ///< Calculated logical SSR output state (true = ON, false = OFF)
};

} // namespace output

