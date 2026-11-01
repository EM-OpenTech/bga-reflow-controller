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
 * @file wifi_manager.cpp
 * @brief Implementation of Wi-Fi SoftAP, NVS Security, mDNS, and Captive Portal DNS.
 *
 * Configures SoftAP mode, starts mDNS responder (reflow.local), persists WPA keys
 * in NVS, and runs UDP DNS redirector on port 53.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/wifi_manager.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr const char* TAG = "WifiManager";

size_t buildDnsResponse(const uint8_t* rx, size_t rxLen, uint8_t* tx, size_t txCap) {
    static constexpr std::array<uint8_t, 16> kDnsAnswerRecord = {
        0xC0, 0x0C,             // Pointer to query name
        0x00, 0x01,             // Type A (Host Address)
        0x00, 0x01,             // Class IN
        0x00, 0x00, 0x00, 0x3C, // TTL: 60 seconds
        0x00, 0x04,             // Data length: 4 bytes
        192,  168,  4,    1     // IP Address: 192.168.4.1
    };

    if (rxLen + kDnsAnswerRecord.size() > txCap) {
        return 0;
    }

    memcpy(tx, rx, rxLen);

    // Flags: Standard query response, No error, Authoritative
    tx[2] = 0x81;
    tx[3] = 0x80;
    // Answer Count = 1
    tx[6] = 0x00;
    tx[7] = 0x01;

    memcpy(tx + rxLen, kDnsAnswerRecord.data(), kDnsAnswerRecord.size());
    return rxLen + kDnsAnswerRecord.size();
}

} // anonymous namespace

namespace web {

// ============================================================================
// Lifecycle & Initialization
// ============================================================================

WifiManager::~WifiManager() {
    if (_dnsSocket >= 0) {
        close(_dnsSocket);
        _dnsSocket = -1;
    }
    if (_dnsTaskHandle != nullptr) {
        vTaskDelete(_dnsTaskHandle);
        _dnsTaskHandle = nullptr;
    }
}

esp_err_t WifiManager::begin() {
    ESP_LOGI(TAG, "Initializing Wi-Fi Manager...");

    // 1. Load credentials from NVS (benign fallback to factory defaults on first boot)
    static_cast<void>(loadCredentialsFromNvs());

    // 2. Initialize SoftAP
    esp_err_t err = startSoftAp();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start SoftAP: %s", esp_err_to_name(err));
        return err;
    }

    // 3. Start mDNS service (reflow.local)
    startMdns();

    // 4. Start Captive Portal DNS Responder (UDP 53)
    startDnsServer();

    std::string ssidCopy = getSsid();
    ESP_LOGI(TAG, "Wi-Fi AP '%s' ready at 192.168.4.1 (mDNS: http://%s.local)", ssidCopy.c_str(), DEFAULT_MDNS_HOST);
    return ESP_OK;
}

// ============================================================================
// NVS Credential Storage
// ============================================================================

esp_err_t WifiManager::loadCredentialsFromNvs() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No Wi-Fi credentials in NVS, using defaults.");
        std::lock_guard<std::mutex> lock(_mutex);
        _ssid = DEFAULT_AP_SSID;
        _password = DEFAULT_AP_PASSWORD;
        _passwordChanged = false;
        return ESP_OK;
    }

    std::array<char, 65> buf{};
    size_t length = buf.size();

    std::string ssidVal = DEFAULT_AP_SSID;
    std::string passVal = DEFAULT_AP_PASSWORD;
    bool changedVal = false;

    if (nvs_get_str(handle, "ap_ssid", buf.data(), &length) == ESP_OK) {
        ssidVal = buf.data();
    }
    length = buf.size();
    if (nvs_get_str(handle, "ap_pass", buf.data(), &length) == ESP_OK) {
        passVal = buf.data();
    }
    uint8_t changed = 0;
    if (nvs_get_u8(handle, "pwd_changed", &changed) == ESP_OK) {
        changedVal = (changed != 0);
    }

    nvs_close(handle);

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _ssid = ssidVal;
        _password = passVal;
        _passwordChanged = changedVal;
    }
    ESP_LOGI(TAG, "Loaded NVS credentials: SSID='%s', pwd_changed=%d", ssidVal.c_str(), changedVal);
    return ESP_OK;
}

esp_err_t WifiManager::saveCredentialsToNvs() const {
    std::string ssidVal;
    std::string passVal;
    bool changedVal = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ssidVal = _ssid;
        passVal = _password;
        changedVal = _passwordChanged;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_str(handle, "ap_ssid", ssidVal.c_str());
    nvs_set_str(handle, "ap_pass", passVal.c_str());
    nvs_set_u8(handle, "pwd_changed", changedVal ? 1 : 0);
    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Saved Wi-Fi credentials to NVS");
    return err;
}

esp_err_t WifiManager::updatePassword(const std::string& newPassword, bool applyLive) {
    if (newPassword.length() < 8 || newPassword.length() > 63) {
        ESP_LOGE(TAG, "Password length must be between 8 and 63 chars");
        return ESP_ERR_INVALID_ARG;
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _password = newPassword;
        _passwordChanged = true;
    }
    esp_err_t err = saveCredentialsToNvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist new credentials to NVS");
        return err;
    }

    if (!applyLive) {
        return ESP_OK;
    }

    return applyLiveConfig();
}

esp_err_t WifiManager::applyLiveConfig() {
    std::string ssidVal;
    std::string passVal;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ssidVal = _ssid;
        passVal = _password;
    }

    // Apply configuration live to Wi-Fi AP
    wifi_config_t wifi_config{};
    std::memset(&wifi_config, 0, sizeof(wifi_config));
    memcpy(wifi_config.ap.ssid, ssidVal.data(), std::min(ssidVal.length(), sizeof(wifi_config.ap.ssid)));
    memcpy(wifi_config.ap.password, passVal.data(), std::min(passVal.length(), sizeof(wifi_config.ap.password)));
    wifi_config.ap.ssid_len = static_cast<uint8_t>(ssidVal.length());
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;

    ESP_LOGI(TAG, "Applying new Wi-Fi password to active AP...");
    return esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
}

esp_err_t WifiManager::confirmDefaultPassword() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _passwordChanged = true;
    }
    return saveCredentialsToNvs();
}

// ============================================================================
// Wi-Fi SoftAP & mDNS Service
// ============================================================================

esp_err_t WifiManager::startSoftAp() {
    _apNetif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t wifi_config{};
    std::memset(&wifi_config, 0, sizeof(wifi_config));
    memcpy(wifi_config.ap.ssid, _ssid.data(), std::min(_ssid.length(), sizeof(wifi_config.ap.ssid)));
    memcpy(wifi_config.ap.password, _password.data(), std::min(_password.length(), sizeof(wifi_config.ap.password)));
    wifi_config.ap.ssid_len = static_cast<uint8_t>(_ssid.length());
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = (_password.empty()) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_WPA3_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    return ESP_OK;
}

esp_err_t WifiManager::startMdns() {
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return err;
    }

    mdns_hostname_set(DEFAULT_MDNS_HOST);
    mdns_instance_name_set(DEFAULT_MDNS_INSTANCE);
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);

    ESP_LOGI(TAG, "mDNS responder started: http://%s.local", DEFAULT_MDNS_HOST);
    return ESP_OK;
}

// ============================================================================
// Captive Portal DNS Server (UDP Port 53 RFC 1035 Redirection)
// ============================================================================

esp_err_t WifiManager::startDnsServer() {
    BaseType_t res = xTaskCreatePinnedToCore(dnsTask, "dns_captive", 3072, this, 3, &_dnsTaskHandle,
                                             0 // Run on Core 0
    );
    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

void WifiManager::dnsTask(void* pvParameters) {
    auto* self = static_cast<WifiManager*>(pvParameters);

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(53);

    self->_dnsSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (self->_dnsSocket < 0) {
        ESP_LOGE(TAG, "Unable to create DNS socket: errno %d", errno);
        self->_dnsTaskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* srvAddr = reinterpret_cast<const struct sockaddr*>(&server_addr);
    if (bind(self->_dnsSocket, srvAddr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS socket unable to bind: errno %d", errno);
        close(self->_dnsSocket);
        self->_dnsSocket = -1;
        self->_dnsTaskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "Captive Portal DNS server listening on UDP port 53");

    std::array<uint8_t, 512> rx_buffer{};
    std::array<uint8_t, 512> tx_buffer{};

    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t client_addr_len = sizeof(client_addr);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        auto* srcAddr = reinterpret_cast<struct sockaddr*>(&client_addr);
        int len = recvfrom(self->_dnsSocket, rx_buffer.data(), rx_buffer.size(), 0, srcAddr, &client_addr_len);

        if (len < 0) {
            // Socket error / non-blocking return -> yield CPU to prevent Task WDT starvation
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (len < 12) {
            continue; // Invalid DNS packet
        }

        size_t txLen = buildDnsResponse(rx_buffer.data(), static_cast<size_t>(len), tx_buffer.data(), tx_buffer.size());
        if (txLen > 0) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            const auto* destAddr = reinterpret_cast<const struct sockaddr*>(&client_addr);
            sendto(self->_dnsSocket, tx_buffer.data(), txLen, 0, destAddr, client_addr_len);
        }
    }
}

} // namespace web
