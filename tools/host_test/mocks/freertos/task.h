// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "FreeRTOS.h"

inline void vTaskDelay(TickType_t) {}

inline BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, int, TaskHandle_t*, int) {
    return pdPASS;
}

inline BaseType_t xTaskCreate(void (*)(void*), const char*, uint32_t, void*, int, TaskHandle_t*) {
    return pdPASS;
}

inline void vTaskDelete(TaskHandle_t) {}

inline uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t) {
    return 4096;
}
