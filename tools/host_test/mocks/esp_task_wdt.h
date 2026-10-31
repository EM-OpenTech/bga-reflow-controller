/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "esp_err.h"

#define CONFIG_ESP_TASK_WDT_EN        1
#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 5

#ifdef __cplusplus
inline bool& mockWdtSubscribed() {
    static bool s_sub = false;
    return s_sub;
}
extern "C" {
#endif

inline esp_err_t esp_task_wdt_add(void* handle) {
    (void)handle;
    mockWdtSubscribed() = true;
    return ESP_OK;
}

inline esp_err_t esp_task_wdt_reset(void) {
    return ESP_OK;
}

inline esp_err_t esp_task_wdt_delete(void* handle) {
    (void)handle;
    mockWdtSubscribed() = false;
    return ESP_OK;
}

inline esp_err_t esp_task_wdt_status(void* handle) {
    (void)handle;
    return mockWdtSubscribed() ? ESP_OK : ESP_ERR_NOT_FOUND;
}

#ifdef __cplusplus
}
#endif
