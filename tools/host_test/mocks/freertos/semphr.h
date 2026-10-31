// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "FreeRTOS.h"
#include <chrono>
#include <mutex>

struct MockMutex {
    std::mutex mtx;
};

struct MockRecursiveMutex {
    std::recursive_mutex mtx;
};

typedef void* SemaphoreHandle_t;

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
    return static_cast<SemaphoreHandle_t>(new MockRecursiveMutex());
}

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return static_cast<SemaphoreHandle_t>(new MockMutex());
}

inline int xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t) {
    if (!s)
        return pdFALSE;
    static_cast<MockRecursiveMutex*>(s)->mtx.lock();
    return pdTRUE;
}

inline int xSemaphoreGiveRecursive(SemaphoreHandle_t s) {
    if (!s)
        return pdFALSE;
    static_cast<MockRecursiveMutex*>(s)->mtx.unlock();
    return pdTRUE;
}

inline int xSemaphoreTake(SemaphoreHandle_t s, TickType_t) {
    if (!s)
        return pdFALSE;
    static_cast<MockMutex*>(s)->mtx.lock();
    return pdTRUE;
}

inline int xSemaphoreGive(SemaphoreHandle_t s) {
    if (!s)
        return pdFALSE;
    static_cast<MockMutex*>(s)->mtx.unlock();
    return pdTRUE;
}

inline void vSemaphoreDelete(SemaphoreHandle_t s) {
    if (s) {
        delete static_cast<MockMutex*>(s);
    }
}
