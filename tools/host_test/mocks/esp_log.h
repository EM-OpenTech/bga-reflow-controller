// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <stdarg.h>
#include <stdio.h>

typedef int (*vprintf_like_t)(const char*, va_list);
inline vprintf_like_t esp_log_set_vprintf(vprintf_like_t) {
    return nullptr;
}

#define ESP_LOGE(tag, fmt, ...)                                                                                        \
    do {                                                                                                               \
    } while (0)
#define ESP_LOGW(tag, fmt, ...)                                                                                        \
    do {                                                                                                               \
    } while (0)
#define ESP_LOGI(tag, fmt, ...)                                                                                        \
    do {                                                                                                               \
    } while (0)
#define ESP_LOGD(tag, fmt, ...)                                                                                        \
    do {                                                                                                               \
    } while (0)
#define ESP_LOGV(tag, fmt, ...)                                                                                        \
    do {                                                                                                               \
    } while (0)
#define ESP_LOG_BUFFER_HEX(tag, buf, len)                                                                              \
    do {                                                                                                               \
    } while (0)

typedef enum {
    ESP_LOG_NONE,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
} esp_log_level_t;

inline void esp_log_level_set(const char*, esp_log_level_t) {}
