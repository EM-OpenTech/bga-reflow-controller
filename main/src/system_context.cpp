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
 * @file system_context.cpp
 * @brief Implementation of thread-safe shared system state context.
 *
 * Manages FreeRTOS mutex synchronization for atomic multi-field telemetry updates
 * and full snapshot extraction.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "system_context.hpp"
#include "esp_log.h"

static const char* TAG = "SystemContext";

namespace app {

SystemContext::SystemContext()
{
    _mutex = xSemaphoreCreateMutex();
}

SystemContext::~SystemContext()
{
    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

bool SystemContext::begin()
{
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
        if (_mutex == nullptr) {
            ESP_LOGE(TAG, "Failed to create FreeRTOS mutex");
            return false;
        }
    }
    return true;
}

bool SystemContext::lock(uint32_t timeoutMs)
{
    if (_mutex == nullptr) return false;
    return (xSemaphoreTake(_mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE);
}

void SystemContext::unlock()
{
    if (_mutex != nullptr) {
        xSemaphoreGive(_mutex);
    }
}

SystemContextData SystemContext::getSnapshot()
{
    SystemContextData copy;
    if (lock(20)) {
        copy = _data;
        unlock();
    }
    return copy;
}

} // namespace app
