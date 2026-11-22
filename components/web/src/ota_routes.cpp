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
 * @file ota_routes.cpp
 * @brief Implementation of Over-The-Air firmware upgrade REST route handler.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/ota_routes.hpp"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "web/api_context.hpp"
#include <array>
#include <utility>

namespace web {

namespace {

constexpr const char* TAG = "OtaRoutes";

bool isProcessActive(const fsm::ReflowFSM* fsm) {
    if (fsm == nullptr) {
        return false;
    }
    return fsm->isProcessActiveState();
}

esp_err_t streamOtaImage(httpd_req_t* req, esp_ota_handle_t ota_handle, size_t total_len) {
    std::array<char, 2048> buf{};
    size_t total_received = 0;
    bool is_first_chunk = true;

    while (total_received < total_len) {
        int received = httpd_req_recv(req, buf.data(), buf.size());
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "OTA receive socket error / timeout (received %zu / %zu bytes)", total_received, total_len);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA receive stream interrupted");
            return ESP_FAIL;
        }

        if (is_first_chunk) {
            if (std::cmp_less(received, sizeof(esp_image_header_t)) ||
                static_cast<uint8_t>(buf[0]) != ESP_IMAGE_HEADER_MAGIC) {
                ESP_LOGE(TAG, "OTA image header invalid: magic byte 0x%02X != 0x%02X", static_cast<uint8_t>(buf[0]),
                         ESP_IMAGE_HEADER_MAGIC);
                esp_ota_abort(ota_handle);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid ESP32 binary header (magic byte mismatch)");
                return ESP_FAIL;
            }
            is_first_chunk = false;
        }

        esp_err_t err = esp_ota_write(ota_handle, buf.data(), static_cast<size_t>(received));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_write failed");
            return err;
        }
        total_received += static_cast<size_t>(received);
    }

    return ESP_OK;
}

void scheduleDelayedReboot() {
    xTaskCreate(
        [](void*) {
            vTaskDelay(pdMS_TO_TICKS(1500));
            ESP_LOGI(TAG, "Rebooting into newly flashed firmware...");
            esp_restart();
        },
        "ota_reboot", 3072, nullptr, 5, nullptr);
}

} // anonymous namespace

esp_err_t OtaRoutes::postOtaUpdateHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (isProcessActive(ctx.fsm)) {
        ESP_LOGW(TAG, "OTA update rejected: process is active (state=%s)", ctx.fsm->getStateString());
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "OTA update forbidden while heating process is active");
        return ESP_OK;
    }

    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty firmware image payload");
        return ESP_OK;
    }

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition available for update");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting OTA update to partition '%s' at offset 0x%" PRIx32 " (size: %zu bytes)...",
             update_partition->label, update_partition->address, req->content_len);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
        return ESP_OK;
    }

    if (streamOtaImage(req, ota_handle, req->content_len) != ESP_OK) {
        return ESP_OK; // Error response already sent in helper
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end validation failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation / digest failed");
        return ESP_OK;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to activate new boot partition");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA update successfully written to '%s'! Scheduling reboot in 1.5s...", update_partition->label);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, R"({"success":true,"message":"OTA update successful! Rebooting...","rebootInMs":2500})");

    scheduleDelayedReboot();
    return ESP_OK;
}

} // namespace web
