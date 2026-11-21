// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file buzzer_player.hpp
 * @brief Non-blocking rhythmic acoustic pattern player for active piezo buzzers.
 *
 * Sequences pulse patterns with microsecond accuracy using the ESP-IDF High-Resolution
 * Hardware Timer (esp_timer), imposing zero CPU overhead while idle.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_timer.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace output {

class OutputManager;

/**
 * @enum BuzzerPattern
 * @brief Rhythmic acoustic patterns for active piezo buzzer notification.
 */
enum class BuzzerPattern : uint8_t {
    NONE = 0,
    PREHEAT_DONE,  ///< Rhythmic double-beep (100 ms ON, 80 ms OFF, 200 ms ON)
    REFLOW_DONE,   ///< Celebratory tri-step (120 ms ON, 80 ms OFF, 120 ms ON, 80 ms OFF, 400 ms ON)
    TIMEOUT_ALERT, ///< Urgent staccato alarm (100 ms ON, 80 ms OFF, 100 ms ON, 80 ms OFF, 100 ms ON, 80 ms OFF, 300 ms
                   ///< ON)
    CLICK          ///< Crisp short feedback pip (40 ms ON)
};

/**
 * @class BuzzerPlayer
 * @brief Autonomous rhythmic pattern sequencer for active piezo buzzers.
 *
 * Utilizes the ESP-IDF High-Resolution Hardware Timer (esp_timer) to drive the
 * physical buzzer GPIO channel on OutputManager with zero CPU overhead in idle state.
 */
class BuzzerPlayer {
public:
    explicit BuzzerPlayer(OutputManager& outputs);
    ~BuzzerPlayer();

    BuzzerPlayer(const BuzzerPlayer&) = delete;
    BuzzerPlayer& operator=(const BuzzerPlayer&) = delete;
    BuzzerPlayer(BuzzerPlayer&&) = delete;
    BuzzerPlayer& operator=(BuzzerPlayer&&) = delete;

    /**
     * @brief Initialize internal esp_timer handle.
     * @return true on success, false on error.
     */
    bool begin();

    /**
     * @brief Play a predefined rhythmic acoustic notification pattern.
     * Starts the pattern sequence immediately (non-blocking).
     * @param pattern Pattern identifier to execute.
     */
    void play(BuzzerPattern pattern);

    /**
     * @brief Immediately silence the buzzer and stop the active pattern.
     */
    void stop();

    /**
     * @brief Advance pattern sequencer by delta time (for unit testing without real-time delay).
     * @param dtMs Elapsed milliseconds.
     */
    void update(uint32_t dtMs);

    /**
     * @brief Enable or disable acoustic feedback on the hardware buzzer.
     * When disabled, all playback commands are ignored and buzzer is forced OFF.
     * @param enabled true to enable acoustic playback, false to silence.
     */
    void setEnabled(bool enabled);

    /**
     * @brief Query whether hardware buzzer playback is enabled.
     * @return true if enabled, false if silenced.
     */
    [[nodiscard]] bool isEnabled() const { return _enabled.load(std::memory_order_relaxed); }

    /**
     * @brief Query whether an acoustic pattern is currently actively playing.
     * @return true if playing, false if idle.
     */
    [[nodiscard]] bool isPlaying() const { return _patternActive.load(std::memory_order_relaxed); }

private:
    struct BuzzerStep {
        uint16_t durationMs = 0;
        bool on = false;
    };

    static constexpr size_t MAX_BUZZER_STEPS = 8;

    OutputManager& _outputs;
    std::atomic<bool> _enabled{true};
    std::atomic<bool> _patternActive{false};

    std::array<BuzzerStep, MAX_BUZZER_STEPS> _activePattern{};
    size_t _patternStepCount = 0;
    size_t _currentStepIndex = 0;
    uint32_t _stepElapsedMs = 0;

    esp_timer_handle_t _timer = nullptr;

    void initTimer();
    void advanceStep();
    static void timerCallback(void* arg);
};

} // namespace output
