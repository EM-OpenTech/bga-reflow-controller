// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file buzzer_player.cpp
 * @brief Implementation of autonomous rhythmic pattern player for active piezo buzzers.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "output/buzzer_player.hpp"
#include "esp_log.h"
#include "output/output_manager.hpp"

namespace {
constexpr const char* TAG = "BUZZER_PLAYER";
} // namespace

namespace output {

// ============================================================================
// Lifecycle & Hardware Timer Initialization
// ============================================================================

BuzzerPlayer::BuzzerPlayer(OutputManager& outputs) : _outputs(outputs) {}

BuzzerPlayer::~BuzzerPlayer() {
    stop();
    if (_timer != nullptr) {
        esp_timer_delete(_timer);
        _timer = nullptr;
    }
}

void BuzzerPlayer::initTimer() {
    if (_timer != nullptr) {
        return;
    }

    const esp_timer_create_args_t timerArgs = {
        .callback = &BuzzerPlayer::timerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "buzzer_player",
        .skip_unhandled_events = true,
    };

    const esp_err_t err = esp_timer_create(&timerArgs, &_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create esp_timer for BuzzerPlayer: %s", esp_err_to_name(err));
    }
}

bool BuzzerPlayer::begin() {
    initTimer();
    return _timer != nullptr;
}

// ============================================================================
// Acoustic Pattern Playback & Control API
// ============================================================================

void BuzzerPlayer::setEnabled(bool enabled) {
    _enabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        stop();
    }
}

void BuzzerPlayer::stop() {
    if (_timer != nullptr) {
        esp_timer_stop(_timer);
    }
    _patternActive.store(false, std::memory_order_relaxed);
    _outputs.setBuzzer(false);
}

void BuzzerPlayer::play(BuzzerPattern pattern) {
    if (!_enabled.load(std::memory_order_relaxed) || pattern == BuzzerPattern::NONE) {
        stop();
        return;
    }

    initTimer();
    if (_timer != nullptr) {
        esp_timer_stop(_timer);
    }

    _patternStepCount = 0;
    switch (pattern) {
    case BuzzerPattern::PREHEAT_DONE:
        // Rhythmic double-beep: 100ms ON, 80ms OFF, 200ms ON (total: 380 ms)
        _activePattern[0] = {100, true};
        _activePattern[1] = {80, false};
        _activePattern[2] = {200, true};
        _patternStepCount = 3;
        break;

    case BuzzerPattern::REFLOW_DONE:
        // Celebratory tri-step: 120ms ON, 80ms OFF, 120ms ON, 80ms OFF, 400ms ON (total: 800 ms)
        _activePattern[0] = {120, true};
        _activePattern[1] = {80, false};
        _activePattern[2] = {120, true};
        _activePattern[3] = {80, false};
        _activePattern[4] = {400, true};
        _patternStepCount = 5;
        break;

    case BuzzerPattern::TIMEOUT_ALERT:
        // Urgent staccato alarm: 100ms ON, 80ms OFF x3, 300ms ON (total: 840 ms)
        _activePattern[0] = {100, true};
        _activePattern[1] = {80, false};
        _activePattern[2] = {100, true};
        _activePattern[3] = {80, false};
        _activePattern[4] = {100, true};
        _activePattern[5] = {80, false};
        _activePattern[6] = {300, true};
        _patternStepCount = 7;
        break;

    case BuzzerPattern::CLICK:
        // Crisp feedback pip: 40ms ON
        _activePattern[0] = {40, true};
        _patternStepCount = 1;
        break;

    case BuzzerPattern::NONE:
        break;
    }

    if (_patternStepCount == 0) {
        stop();
        return;
    }

    _currentStepIndex = 0;
    _stepElapsedMs = 0;
    _patternActive.store(true, std::memory_order_relaxed);

    const bool initialOn = _activePattern[0].on && _enabled.load(std::memory_order_relaxed);
    _outputs.setBuzzer(initialOn);

    if (_timer != nullptr) {
        esp_timer_start_once(_timer, static_cast<uint64_t>(_activePattern[0].durationMs) * 1000ULL);
    }
}

// ============================================================================
// Timer Callback & Step Sequencer
// ============================================================================

void BuzzerPlayer::timerCallback(void* arg) {
    if (arg != nullptr) {
        static_cast<BuzzerPlayer*>(arg)->advanceStep();
    }
}

void BuzzerPlayer::advanceStep() {
    if (!_patternActive.load(std::memory_order_relaxed)) {
        return;
    }

    _currentStepIndex++;
    if (_currentStepIndex >= _patternStepCount) {
        _patternActive.store(false, std::memory_order_relaxed);
        _outputs.setBuzzer(false);
        return;
    }

    const auto& step = _activePattern[_currentStepIndex];
    const bool stepOn = step.on && _enabled.load(std::memory_order_relaxed);
    _outputs.setBuzzer(stepOn);

    if (_timer != nullptr) {
        esp_timer_start_once(_timer, static_cast<uint64_t>(step.durationMs) * 1000ULL);
    }
}

void BuzzerPlayer::update(uint32_t dtMs) {
    if (!_patternActive.load(std::memory_order_relaxed)) {
        return;
    }

    _stepElapsedMs += dtMs;
    while (_patternActive.load(std::memory_order_relaxed) && _currentStepIndex < _patternStepCount &&
           _stepElapsedMs >= _activePattern[_currentStepIndex].durationMs) {
        _stepElapsedMs -= _activePattern[_currentStepIndex].durationMs;
        advanceStep();
    }
}

} // namespace output
