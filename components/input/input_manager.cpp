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

static const char* TAG = "INPUT_MGR";

namespace input {

// ============================================================================
// Lifecycle & GPIO Hardware Initialization
// ============================================================================

InputManager::InputManager(const InputSystemConfig& sysConfig)
    : _sysConfig(sysConfig) {}

bool InputManager::begin() {
    bool ok = true;
    ok &= initChannel(_sysConfig.btnStart);
    ok &= initChannel(_sysConfig.btnStop);
    ok &= initChannel(_sysConfig.swFan);
    ok &= initChannel(_sysConfig.swLamp);

    if (ok) {
        ESP_LOGI(TAG, "InputManager initialized: Start(GPIO %d) Stop(GPIO %d) SwFan(GPIO %d) SwLamp(GPIO %d)",
                 (int)_sysConfig.btnStart.pin,
                 (int)_sysConfig.btnStop.pin,
                 (int)_sysConfig.swFan.pin,
                 (int)_sysConfig.swLamp.pin);
    } else {
        ESP_LOGE(TAG, "Failed to initialize one or more GPIO input pins!");
    }
    return ok;
}

bool InputManager::initChannel(const InputChannelConfig& chConfig) {
    if (chConfig.pin == GPIO_NUM_NC) return true;

    gpio_config_t io = {};
    io.mode         = GPIO_MODE_INPUT;
    io.pin_bit_mask = (1ULL << chConfig.pin);
    io.pull_up_en   = chConfig.pullUp;
    io.pull_down_en = chConfig.pullDown;
    io.intr_type    = GPIO_INTR_DISABLE;

    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed for input pin %d: %s", (int)chConfig.pin, esp_err_to_name(ret));
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
    updateButton(_sysConfig.btnStop,  _stopTracker,  _onStopCb);

    // Latching toggle switches: track continuous state, fire callback on state change
    updateSwitch(_sysConfig.swFan,  _swFanTracker,  _onFanSwitchCb);
    updateSwitch(_sysConfig.swLamp, _swLampTracker, _onLampSwitchCb);
}

void InputManager::updateButton(const InputChannelConfig& chConfig,
                                 InputStateTracker& tracker,
                                 ButtonCallback cb) {
    if (chConfig.pin == GPIO_NUM_NC) return;

    int rawLevel     = gpio_get_level(chConfig.pin);
    bool isPressedNow = chConfig.activeLow ? (rawLevel == 0) : (rawLevel == 1);
    uint64_t nowUs   = static_cast<uint64_t>(esp_timer_get_time());
    uint64_t debounceUs = static_cast<uint64_t>(chConfig.debounceMs) * 1000ULL;

    if (isPressedNow != tracker.rawState) {
        tracker.rawState = isPressedNow;
        tracker.lastStateChangeUs = nowUs;
    } else if ((nowUs - tracker.lastStateChangeUs) >= debounceUs) {
        if (isPressedNow != tracker.debouncedState) {
            tracker.debouncedState = isPressedNow;
            // Only trigger event on press (leading edge), not on release
            if (isPressedNow) {
                tracker.wasPressedEvent = true;
                if (cb) cb();
            }
        }
    }
}

void InputManager::updateSwitch(const InputChannelConfig& chConfig,
                                 InputStateTracker& tracker,
                                 SwitchCallback cb) {
    if (chConfig.pin == GPIO_NUM_NC) return;

    int rawLevel     = gpio_get_level(chConfig.pin);
    bool isActiveNow = chConfig.activeLow ? (rawLevel == 0) : (rawLevel == 1);
    uint64_t nowUs   = static_cast<uint64_t>(esp_timer_get_time());
    uint64_t debounceUs = static_cast<uint64_t>(chConfig.debounceMs) * 1000ULL;

    if (isActiveNow != tracker.rawState) {
        tracker.rawState = isActiveNow;
        tracker.lastStateChangeUs = nowUs;
    } else if ((nowUs - tracker.lastStateChangeUs) >= debounceUs) {
        if (isActiveNow != tracker.debouncedState) {
            tracker.debouncedState = isActiveNow;
            // Fire callback on both ON and OFF transitions for latching switches
            if (cb) {
                cb(isActiveNow);
            }
            ESP_LOGI(TAG, "Toggle switch GPIO %d -> %s",
                     (int)chConfig.pin, isActiveNow ? "ON (Closed)" : "OFF (Open)");
        }
    }
}

// ============================================================================
// Event Latch & State Getters
// ============================================================================

bool InputManager::wasStartPressed() {
    if (_startTracker.wasPressedEvent) {
        _startTracker.wasPressedEvent = false;
        return true;
    }
    return false;
}

bool InputManager::wasStopPressed() {
    if (_stopTracker.wasPressedEvent) {
        _stopTracker.wasPressedEvent = false;
        return true;
    }
    return false;
}

} // namespace input
