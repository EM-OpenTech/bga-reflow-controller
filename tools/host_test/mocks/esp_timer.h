// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "esp_err.h"
#include <chrono>
#include <cstdint>

typedef struct esp_timer* esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void* arg);

typedef enum { ESP_TIMER_TASK, ESP_TIMER_ISR } esp_timer_dispatch_t;

typedef struct {
    esp_timer_cb_t callback;
    void* arg;
    esp_timer_dispatch_t dispatch_method;
    const char* name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;

struct esp_timer {
    esp_timer_cb_t callback = nullptr;
    void* arg = nullptr;
    bool active = false;
    uint64_t timeout_us = 0;
};

inline int64_t esp_timer_get_time() {
    using namespace std::chrono;
    static const auto startTime = steady_clock::now();
    return duration_cast<microseconds>(steady_clock::now() - startTime).count();
}

inline esp_err_t esp_timer_create(const esp_timer_create_args_t* create_args, esp_timer_handle_t* out_handle) {
    if (create_args == nullptr || out_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    auto* timer = new esp_timer();
    timer->callback = create_args->callback;
    timer->arg = create_args->arg;
    timer->active = false;
    *out_handle = timer;
    return ESP_OK;
}

inline esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    timer->active = true;
    timer->timeout_us = timeout_us;
    return ESP_OK;
}

inline esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    timer->active = false;
    return ESP_OK;
}

inline esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    delete timer;
    return ESP_OK;
}
