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
 * @file main.cpp
 * @brief BGA Reflow Controller Main Application Entry Point.
 *
 * Executes optional boot-time unit testing, initializes NVS, network interface,
 * default event loop, and starts the AppController master manager.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "sdkconfig.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_controller.hpp"

#if CONFIG_REFLOW_ENABLE_UNIT_TESTS
#include "test_runner.hpp"
#endif

static const char *TAG = "MAIN";

// ============================================================================
// Application Entry Point & Subsystem Bootstrap
// ============================================================================

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "   BGA REFLOW CONTROLLER FIRMWARE                ");
    ESP_LOGI(TAG, "=================================================");

#if CONFIG_REFLOW_ENABLE_UNIT_TESTS
    // 0. Run Automated Unit Tests across all 11 Modules
    ESP_LOGI(TAG, "Boot-time Unit Tests enabled (CONFIG_REFLOW_ENABLE_UNIT_TESTS=y)");
    if (!app::runAllUnitTests()) {
#if CONFIG_REFLOW_HALT_ON_TEST_FAILURE
        ESP_LOGE(TAG, "FATAL: Unit tests reported failures! Halting boot sequence for safety.");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
#else
        ESP_LOGW(TAG, "WARNING: Unit tests reported failures, but halting is disabled. Continuing boot...");
#endif
    }
#else
    ESP_LOGI(TAG, "Boot-time Unit Tests disabled (CONFIG_REFLOW_ENABLE_UNIT_TESTS=n) – fast boot.");
#endif

    // 1. Initialize Essential ESP-IDF Infrastructure
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2. Instantiate and Initialize Master Application Controller
    static app::AppController appInstance;
    if (!appInstance.begin()) {
        ESP_LOGE(TAG, "Fatal Error: AppController initialization failed!");
        return;
    }

    // 3. Spawn FreeRTOS Tasks (Core 0: Web/Inputs, Core 1: Control/Safety)
    appInstance.startTasks();

    ESP_LOGI(TAG, "Boot sequence completed successfully. FreeRTOS scheduler active.");
}
