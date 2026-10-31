// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
typedef void* QueueHandle_t;
typedef int BaseType_t;

#define portTICK_PERIOD_MS 1
#define portMAX_DELAY      ((TickType_t)0xffffffffUL)
#define pdMS_TO_TICKS(ms)  ((TickType_t)(ms))
#define pdTRUE             1
#define pdFALSE            0
#define pdPASS             1
#define portYIELD_FROM_ISR()                                                                                           \
    do {                                                                                                               \
    } while (0)

inline BaseType_t xPortInIsrContext() {
    return 0;
}

#ifndef ESP_ERROR_CHECK
#define ESP_ERROR_CHECK(x)                                                                                             \
    do {                                                                                                               \
        (void)(x);                                                                                                     \
    } while (0)
#endif
