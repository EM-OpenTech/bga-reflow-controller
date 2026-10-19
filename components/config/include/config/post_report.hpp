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
 * @file post_report.hpp
 * @brief Power-On Self-Test (POST) and post-mortem boot diagnostics data model.
 *
 * Serves as the Single Source of Truth for system boot diagnostics, shared
 * directly between AppController and the Web REST API status endpoint.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_system.h"
#include <cstddef>
#include <cstdint>

namespace config {

/**
 * @brief Power-On Self-Test (POST) and post-mortem boot diagnosis report.
 */
struct PostReport {
    esp_reset_reason_t resetReason = ESP_RST_UNKNOWN;
    const char* resetReasonStr = "UNKNOWN";
    bool wasWatchdogReset = false; ///< True if previous reboot was Task/Interrupt WDT
    bool wasBrownoutReset = false; ///< True if previous reboot was voltage dip
    size_t freeHeapBytes = 0;      ///< Free internal SRAM heap (bytes)
    size_t freePsramBytes = 0;     ///< Free Octal PSRAM (bytes)
    bool littleFsOk = false;       ///< Flash filesystem mount status
    size_t littleFsFreeBytes = 0;  ///< Free LittleFS storage space (bytes)
    bool topSensorOk = false;      ///< Top MAX31856 SPI probe status
    float topCjTemp = 0.0f;        ///< Top cold-junction probe temperature (°C)
    float topRawTemp = 0.0f;       ///< Top thermocouple probe temperature (°C)
    bool bottomSensorOk = false;   ///< Bottom MAX31856 SPI probe status
    float bottomCjTemp = 0.0f;     ///< Bottom cold-junction probe temperature (°C)
    float bottomRawTemp = 0.0f;    ///< Bottom thermocouple probe temperature (°C)
    bool outputsSafe = false;      ///< GPIO SSR safe-state verified (0V)
    bool allPassed = false;        ///< True if all critical POST stages passed
};

} // namespace config
