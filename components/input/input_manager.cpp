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
 * @file input_manager.cpp
 * @brief Implementation of hardware input manager with software debouncing.
 *
 * Implements non-blocking microsecond timing debouncing via esp_timer_get_time()
 * and configures physical GPIO inputs using the ESP-IDF driver/gpio API.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "input/input_manager.hpp"
#include "esp_log.h"
#include <algorithm>
#include <array>
#include <utility>

namespace {

constexpr const char* TAG = "INPUT_MGR";

bool readLogicalLevel(const input::InputChannelConfig& chConfig) {
    int rawLevel = gpio_get_level(chConfig.pin);
    return chConfig.activeLow ? (rawLevel == 0) : (rawLevel == 1);
}

} // anonymous namespace

namespace input {

// ============================================================================
// Input System Configuration Validation
// ============================================================================

bool InputSystemConfig::validate() const {
    const std::array<const InputChannelConfig*, 4> channels = {&btnStart, &btnStop, &swFan, &swLamp};

    for (size_t i = 0; i < channels.size(); ++i) {
        const auto* ch = channels[i];
        if (ch->pin == GPIO_NUM_NC) {
            continue;
        }

        // 1. Guard against ESP32-S3 Octal PSRAM range (GPIO 35-42)
        if (ch->pin >= GPIO_NUM_35 && ch->pin <= GPIO_NUM_42) {
            return false;
        }

        // 2. Electrical conflict: cannot enable both pull-up and pull-down simultaneously
        if (ch->pullUp == GPIO_PULLUP_ENABLE && ch->pullDown == GPIO_PULLDOWN_ENABLE) {
            return false;
        }

        // 3. Debounce range validation (1 ms .. 1000 ms)
        if (ch->debounceMs == 0 || ch->debounceMs > 1000) {
            return false;
        }

        // 4. Check for pin collisions
        const auto currentPin = ch->pin;
        if (std::any_of(
                channels.begin() + static_cast<ptrdiff_t>(i + 1), channels.end(),
                [currentPin](const auto* other) { return other->pin != GPIO_NUM_NC && other->pin == currentPin; })) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Lifecycle & GPIO Hardware Initialization
// ============================================================================

InputManager::InputManager(const InputSystemConfig& sysConfig) : _sysConfig(sysConfig) {}

bool InputManager::begin() const {
    if (!_sysConfig.validate()) {
        ESP_LOGE(TAG, "InputSystemConfig validation failed: illegal pins, collisions or electrical conflict");
        return false;
    }

    bool ok = true;
    ok &= initChannel(_sysConfig.btnStart);
    ok &= initChannel(_sysConfig.btnStop);
    ok &= initChannel(_sysConfig.swFan);
    ok &= initChannel(_sysConfig.swLamp);

    if (!ok) {
        ESP_LOGE(TAG, "Failed to initialize one or more GPIO input pins!");
        return false;
    }

    ESP_LOGI(TAG, "InputManager initialized: Start(GPIO %d) Stop(GPIO %d) SwFan(GPIO %d) SwLamp(GPIO %d)",
             static_cast<int>(_sysConfig.btnStart.pin), static_cast<int>(_sysConfig.btnStop.pin),
             static_cast<int>(_sysConfig.swFan.pin), static_cast<int>(_sysConfig.swLamp.pin));
    return true;
}

bool InputManager::initChannel(const InputChannelConfig& chConfig) {
    if (chConfig.pin == GPIO_NUM_NC) {
        return true;
    }

    if (chConfig.pullUp == GPIO_PULLUP_ENABLE && chConfig.pullDown == GPIO_PULLDOWN_ENABLE) {
        ESP_LOGE(TAG, "Pin %d: Invalid config: Pull-Up and Pull-Down cannot both be enabled simultaneously!",
                 static_cast<int>(chConfig.pin));
        return false;
    }

    gpio_config_t io = {};
    io.mode = GPIO_MODE_INPUT;
    io.pin_bit_mask = (1ULL << chConfig.pin);
    io.pull_up_en = chConfig.pullUp;
    io.pull_down_en = chConfig.pullDown;
    io.intr_type = GPIO_INTR_DISABLE;

    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed for input pin %d: %s", static_cast<int>(chConfig.pin), esp_err_to_name(ret));
        return false;
    }
    return true;
}

// ============================================================================
// Periodic State Polling & Debounce Engine
// ============================================================================

void InputManager::update() {
    // Momentary push-buttons: trigger one-shot click event on press
    updateButton(_sysConfig.btnStart, _startTracker, _onStartCb);
    updateButton(_sysConfig.btnStop, _stopTracker, _onStopCb);

    // Latching toggle switches: track continuous state, fire callback on state change
    updateSwitch(_sysConfig.swFan, _swFanTracker, _onFanSwitchCb);
    updateSwitch(_sysConfig.swLamp, _swLampTracker, _onLampSwitchCb);
}

void InputManager::updateButton(const InputChannelConfig& chConfig, InputStateTracker& tracker,
                                const ButtonCallback& cb) {
    if (chConfig.pin == GPIO_NUM_NC) {
        return;
    }

    bool isPressedNow = readLogicalLevel(chConfig);
    const auto nowUs = static_cast<uint64_t>(esp_timer_get_time());
    uint64_t debounceUs = static_cast<uint64_t>(chConfig.debounceMs) * 1000ULL;

    // 1. Raw state transition (edge detected / contact bounce started)
    if (isPressedNow != tracker.rawState) {
        tracker.rawState = isPressedNow;
        tracker.lastStateChangeUs = nowUs;
        return;
    }

    // 2. Debounce window guard: signal must remain continuously stable
    if ((nowUs - tracker.lastStateChangeUs) < debounceUs) {
        return;
    }

    // 3. State change guard: nothing changed after debounce
    if (isPressedNow == tracker.debouncedState) {
        return;
    }

    // 4. Stable state confirmed
    tracker.debouncedState = isPressedNow;

    // Only trigger event on press (leading edge), not on release
    if (isPressedNow) {
        tracker.wasPressedEvent = true;
        if (cb != nullptr) {
            cb();
        }
    }
}

void InputManager::updateSwitch(const InputChannelConfig& chConfig, InputStateTracker& tracker,
                                const SwitchCallback& cb) {
    if (chConfig.pin == GPIO_NUM_NC) {
        return;
    }

    bool isActiveNow = readLogicalLevel(chConfig);
    const auto nowUs = static_cast<uint64_t>(esp_timer_get_time());
    uint64_t debounceUs = static_cast<uint64_t>(chConfig.debounceMs) * 1000ULL;

    // 1. Raw state transition (edge detected / contact bounce started)
    if (isActiveNow != tracker.rawState) {
        tracker.rawState = isActiveNow;
        tracker.lastStateChangeUs = nowUs;
        return;
    }

    // 2. Debounce window guard: signal must remain continuously stable
    if ((nowUs - tracker.lastStateChangeUs) < debounceUs) {
        return;
    }

    // 3. State change guard: nothing changed after debounce
    if (isActiveNow == tracker.debouncedState) {
        return;
    }

    // 4. Stable state confirmed
    tracker.debouncedState = isActiveNow;

    // Fire callback on both ON and OFF transitions for latching switches
    if (cb != nullptr) {
        cb(isActiveNow);
    }
    ESP_LOGI(TAG, "Toggle switch GPIO %d -> %s", static_cast<int>(chConfig.pin),
             isActiveNow ? "ON (Closed)" : "OFF (Open)");
}

// ============================================================================
// Event Latch & State Getters
// ============================================================================

bool InputManager::wasStartPressed() {
    return std::exchange(_startTracker.wasPressedEvent, false);
}

bool InputManager::wasStopPressed() {
    return std::exchange(_stopTracker.wasPressedEvent, false);
}

} // namespace input
