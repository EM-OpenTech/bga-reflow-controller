// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <chrono>
#include <stdint.h>

inline int64_t esp_timer_get_time() {
    using namespace std::chrono;
    static const auto startTime = steady_clock::now();
    return duration_cast<microseconds>(steady_clock::now() - startTime).count();
}
