/**
 * @file system_context.cpp
 * @brief Thread-safe shared context implementation using FreeRTOS mutexes.
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
