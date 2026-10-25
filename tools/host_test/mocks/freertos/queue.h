// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "FreeRTOS.h"
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <vector>

struct MockQueue {
    size_t itemSize;
    size_t maxItems;
    std::mutex mtx;
    std::condition_variable cv;
    std::queue<std::vector<uint8_t>> items;

    MockQueue(size_t len, size_t size) : itemSize(size), maxItems(len) {}
};

inline QueueHandle_t xQueueCreate(uint32_t len, uint32_t itemSize) {
    return static_cast<QueueHandle_t>(new MockQueue(len, itemSize));
}

inline void vQueueDelete(QueueHandle_t q) {
    if (q) {
        delete static_cast<MockQueue*>(q);
    }
}

inline int xQueueSend(QueueHandle_t q, const void* itemToQueue, TickType_t ticksToWait) {
    if (!q || !itemToQueue) return pdFALSE;
    auto* mq = static_cast<MockQueue*>(q);
    std::unique_lock<std::mutex> lock(mq->mtx);
    if (mq->items.size() >= mq->maxItems) {
        if (ticksToWait == 0) return pdFALSE;
        mq->cv.wait_for(lock, std::chrono::milliseconds(ticksToWait), [&]() {
            return mq->items.size() < mq->maxItems;
        });
        if (mq->items.size() >= mq->maxItems) return pdFALSE;
    }
    std::vector<uint8_t> buf(mq->itemSize);
    std::memcpy(buf.data(), itemToQueue, mq->itemSize);
    mq->items.push(std::move(buf));
    mq->cv.notify_one();
    return pdTRUE;
}

inline int xQueueReceive(QueueHandle_t q, void* buffer, TickType_t ticksToWait) {
    if (!q || !buffer) return pdFALSE;
    auto* mq = static_cast<MockQueue*>(q);
    std::unique_lock<std::mutex> lock(mq->mtx);
    if (mq->items.empty()) {
        if (ticksToWait == 0) return pdFALSE;
        mq->cv.wait_for(lock, std::chrono::milliseconds(ticksToWait), [&]() {
            return !mq->items.empty();
        });
        if (mq->items.empty()) return pdFALSE;
    }
    const auto& front = mq->items.front();
    std::memcpy(buffer, front.data(), mq->itemSize);
    mq->items.pop();
    mq->cv.notify_one();
    return pdTRUE;
}

inline int xQueueSendFromISR(QueueHandle_t q, const void* itemToQueue, BaseType_t* pxHigherPriorityTaskWoken) {
    if (pxHigherPriorityTaskWoken != nullptr) {
        *pxHigherPriorityTaskWoken = pdFALSE;
    }
    return xQueueSend(q, itemToQueue, 0);
}
