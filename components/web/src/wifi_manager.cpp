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
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <cstring>

static const char* TAG = "WifiManager";

namespace web {

WifiManager::~WifiManager()
{
    if (_dnsSocket >= 0) {
        close(_dnsSocket);
        _dnsSocket = -1;
    }
    if (_dnsTaskHandle != nullptr) {
        vTaskDelete(_dnsTaskHandle);
        _dnsTaskHandle = nullptr;
    }
}

esp_err_t WifiManager::begin()
{
    ESP_LOGI(TAG, "Initializing Wi-Fi Manager...");

    // 1. Load credentials from NVS
    loadCredentialsFromNvs();

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
    ESP_LOGI(TAG, "Wi-Fi AP '%s' ready at 192.168.4.1 (mDNS: http://%s.local)",
             ssidCopy.c_str(), DEFAULT_MDNS_HOST);
    return ESP_OK;
}

esp_err_t WifiManager::loadCredentialsFromNvs()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No Wi-Fi credentials in NVS, using defaults.");
        std::lock_guard<std::mutex> lock(_mutex);
        _ssid = DEFAULT_AP_SSID;
        _password = DEFAULT_AP_PASSWORD;
        _passwordChanged = false;
        return ESP_OK;
    }

    char buf[65] = {0};
    size_t length = sizeof(buf);

    std::string ssidVal = DEFAULT_AP_SSID;
    std::string passVal = DEFAULT_AP_PASSWORD;
    bool changedVal = false;

    if (nvs_get_str(handle, "ap_ssid", buf, &length) == ESP_OK) {
        ssidVal = buf;
    }
    length = sizeof(buf);
    if (nvs_get_str(handle, "ap_pass", buf, &length) == ESP_OK) {
        passVal = buf;
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

esp_err_t WifiManager::saveCredentialsToNvs()
{
    std::string ssidVal;
    std::string passVal;
    bool changedVal;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ssidVal = _ssid;
        passVal = _password;
        changedVal = _passwordChanged;
    }

    nvs_handle_t handle;
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

esp_err_t WifiManager::updatePassword(const std::string& newPassword)
{
    if (newPassword.length() < 8 || newPassword.length() > 63) {
        ESP_LOGE(TAG, "Password length must be between 8 and 63 chars");
        return ESP_ERR_INVALID_ARG;
    }

    std::string ssidVal;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _password = newPassword;
        _passwordChanged = true;
        ssidVal = _ssid;
    }
    saveCredentialsToNvs();

    // Apply configuration live to Wi-Fi AP
    wifi_config_t wifi_config = {};
    strlcpy((char*)wifi_config.ap.ssid, ssidVal.c_str(), sizeof(wifi_config.ap.ssid));
    strlcpy((char*)wifi_config.ap.password, newPassword.c_str(), sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = ssidVal.length();
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;

    ESP_LOGI(TAG, "Applying new Wi-Fi password to active AP...");
    return esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
}

esp_err_t WifiManager::confirmDefaultPassword()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _passwordChanged = true;
    }
    return saveCredentialsToNvs();
}

esp_err_t WifiManager::startSoftAp()
{
    _apNetif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t wifi_config = {};
    strlcpy((char*)wifi_config.ap.ssid, _ssid.c_str(), sizeof(wifi_config.ap.ssid));
    strlcpy((char*)wifi_config.ap.password, _password.c_str(), sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = _ssid.length();
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = (_password.empty()) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_WPA3_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    return ESP_OK;
}

esp_err_t WifiManager::startMdns()
{
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

esp_err_t WifiManager::startDnsServer()
{
    BaseType_t res = xTaskCreatePinnedToCore(
        dnsTask,
        "dns_captive",
        3072,
        this,
        3,
        &_dnsTaskHandle,
        0 // Run on Core 0
    );
    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

void WifiManager::dnsTask(void* pvParameters)
{
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

    if (bind(self->_dnsSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS socket unable to bind: errno %d", errno);
        close(self->_dnsSocket);
        self->_dnsSocket = -1;
        self->_dnsTaskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "Captive Portal DNS server listening on UDP port 53");

    uint8_t rx_buffer[512];
    uint8_t tx_buffer[512];

    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t client_addr_len = sizeof(client_addr);

        int len = recvfrom(self->_dnsSocket, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr*)&client_addr, &client_addr_len);

        if (len < 12) {
            continue; // Invalid DNS packet
        }

        // Prepare DNS response (Header + Original Query + Answer)
        memcpy(tx_buffer, rx_buffer, len);

        // Flags: Standard query response, No error, Authoritative
        tx_buffer[2] = 0x81;
        tx_buffer[3] = 0x80;
        // Answer Count = 1
        tx_buffer[6] = 0x00;
        tx_buffer[7] = 0x01;

        int tx_len = len;

        // Append Answer Record (Pointer to Question, Type A, Class IN, TTL 60s, IP 192.168.4.1)
        uint8_t answer[] = {
            0xC0, 0x0C,             // Pointer to query name
            0x00, 0x01,             // Type A (Host Address)
            0x00, 0x01,             // Class IN
            0x00, 0x00, 0x00, 0x3C, // TTL: 60 seconds
            0x00, 0x04,             // Data length: 4 bytes
            192, 168, 4, 1          // IP Address: 192.168.4.1
        };

        if (tx_len + sizeof(answer) <= sizeof(tx_buffer)) {
            memcpy(tx_buffer + tx_len, answer, sizeof(answer));
            tx_len += sizeof(answer);

            sendto(self->_dnsSocket, tx_buffer, tx_len, 0,
                   (struct sockaddr*)&client_addr, client_addr_len);
        }
    }
}

} // namespace web
