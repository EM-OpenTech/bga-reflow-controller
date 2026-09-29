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
 * @file output_manager.cpp
 * @brief Implementation of hardware GPIO output manager.
 *
 * Implements safe GPIO driver configuration, polarity inversion handling,
 * and hardware safety inhibit lockout enforcement for all actuators.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "output/output_manager.hpp"
#include "esp_log.h"

static const char* TAG = "OUTPUT_MGR";

namespace output {

OutputManager::OutputManager(const OutputSystemConfig& sysConfig)
    : _sysConfig(sysConfig) {}

bool OutputManager::begin() {
    bool ok = true;
    ok &= initChannel(_sysConfig.ssrTop);
    ok &= initChannel(_sysConfig.ssrBottom);
    ok &= initChannel(_sysConfig.fan);
    ok &= initChannel(_sysConfig.lamp);
    ok &= initChannel(_sysConfig.buzzer);

    if (ok) {
        ESP_LOGI(TAG, "All GPIO outputs initialized safely in OFF state.");
    } else {
        ESP_LOGE(TAG, "Failed to initialize one or more GPIO outputs!");
    }
    return ok;
}

bool OutputManager::initChannel(const OutputChannelConfig& chConfig) {
    if (chConfig.pin == GPIO_NUM_NC) return true;

    gpio_config_t io = {};
    io.mode         = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (1ULL << chConfig.pin);
    io.pull_down_en = chConfig.pullDown;
    io.pull_up_en   = chConfig.pullUp;
    io.intr_type    = GPIO_INTR_DISABLE;

    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed for pin %d: %s", (int)chConfig.pin, esp_err_to_name(ret));
        return false;
    }

    // Initialize pin to OFF (inactive) state
    writeChannel(chConfig, false);
    return true;
}

void OutputManager::writeChannel(const OutputChannelConfig& chConfig, bool active) {
    if (chConfig.pin == GPIO_NUM_NC) return;

    // Active-HIGH: active=true -> 1, active=false -> 0
    // Active-LOW:  active=true -> 0, active=false -> 1 (Inverted logic)
    int level = active ? (chConfig.activeHigh ? 1 : 0) : (chConfig.activeHigh ? 0 : 1);
    gpio_set_level(chConfig.pin, level);
}

void OutputManager::setSsrTop(bool on) {
    _ssrTopState = on;
    // Suppress physical SSR activation if safety inhibit is active
    writeChannel(_sysConfig.ssrTop, _inhibit ? false : on);
}

void OutputManager::setSsrBottom(bool on) {
    _ssrBottomState = on;
    // Suppress physical SSR activation if safety inhibit is active
    writeChannel(_sysConfig.ssrBottom, _inhibit ? false : on);
}

void OutputManager::setFan(bool on) {
    _fanState = on;
    writeChannel(_sysConfig.fan, on);
}

void OutputManager::setLamp(bool on) {
    _lampState = on;
    writeChannel(_sysConfig.lamp, on);
}

void OutputManager::setBuzzer(bool on) {
    _buzzerState = on;
    writeChannel(_sysConfig.buzzer, on);
}

void OutputManager::setInhibit(bool inhibit) {
    _inhibit = inhibit;
    if (_inhibit) {
        // Immediately override and drive SSR outputs to physical OFF state
        writeChannel(_sysConfig.ssrTop, false);
        writeChannel(_sysConfig.ssrBottom, false);
        ESP_LOGW(TAG, "SAFETY INHIBIT ACTIVATED! All SSR outputs forced OFF.");
    }
}

void OutputManager::setChannelPolarity(gpio_num_t pin, bool activeHigh) {
    // Dynamic runtime update of Active-HIGH vs Active-LOW inverted logic
    if (_sysConfig.ssrTop.pin == pin) _sysConfig.ssrTop.activeHigh = activeHigh;
    else if (_sysConfig.ssrBottom.pin == pin) _sysConfig.ssrBottom.activeHigh = activeHigh;
    else if (_sysConfig.fan.pin == pin) _sysConfig.fan.activeHigh = activeHigh;
    else if (_sysConfig.lamp.pin == pin) _sysConfig.lamp.activeHigh = activeHigh;
    else if (_sysConfig.buzzer.pin == pin) _sysConfig.buzzer.activeHigh = activeHigh;
}

} // namespace output
