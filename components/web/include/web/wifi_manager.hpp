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
 * @file wifi_manager.hpp
 * @brief Wi-Fi SoftAP, NVS Security, mDNS, and Captive Portal DNS Manager.
 *
 * Coordinates Wi-Fi Access Point configuration, credential persistence in NVS,
 * mDNS service discovery (http://reflow.local), and asynchronous DNS redirection (port 53).
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include <cstdint>
#include <string>
#include <mutex>
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"

namespace web {

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

/// Default Wi-Fi Access Point SSID
constexpr const char* DEFAULT_AP_SSID = "BGA Reflow Controller";

/// Default Wi-Fi Access Point Password (changeable via NVS / UI)
constexpr const char* DEFAULT_AP_PASSWORD = "reflow123";

/// Default mDNS Hostname (reachable at http://reflow.local)
constexpr const char* DEFAULT_MDNS_HOST = "reflow";

/// Default mDNS Instance Name
constexpr const char* DEFAULT_MDNS_INSTANCE = "BGA Reflow Controller";

/// NVS Namespace for Wi-Fi Security
constexpr const char* NVS_WIFI_NAMESPACE = "wifi_sec";

/**
 * @class WifiManager
 * @brief Manages SoftAP Wi-Fi, NVS credentials storage, mDNS, and Captive Portal DNS.
 * 
 * Features:
 * - Stores SSID, Password, and PasswordChanged flag securely in NVS.
 * - WPA2 / WPA3 mixed authentication mode.
 * - Starts mDNS (http://reflow.local).
 * - Runs asynchronous Captive Portal DNS server (Port 53 UDP) redirecting to 192.168.4.1.
 */
class WifiManager {
public:
    WifiManager() = default;
    ~WifiManager();

    /**
     * @brief Initialize NVS, configure SoftAP Wi-Fi, start mDNS, and start Captive DNS.
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t begin();

    /**
     * @brief Check whether the user has customized/confirmed the AP password.
     */
    bool isPasswordChanged() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _passwordChanged;
    }

    /**
     * @brief Get current active SSID.
     */
    std::string getSsid() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _ssid;
    }

    /**
     * @brief Update AP password in NVS and apply to running Wi-Fi AP.
     * 
     * @param newPassword New WPA2/WPA3 password (min 8 chars, max 63 chars).
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t updatePassword(const std::string& newPassword);

    /**
     * @brief Mark the current password as confirmed/accepted by user.
     */
    esp_err_t confirmDefaultPassword();

private:
    mutable std::mutex _mutex;
    std::string _ssid            = DEFAULT_AP_SSID;     ///< Active Access Point SSID
    std::string _password        = DEFAULT_AP_PASSWORD; ///< Active Access Point WPA passphrase
    bool        _passwordChanged = false;                ///< Flag indicating if user changed default password

    esp_netif_t* _apNetif        = nullptr;             ///< ESP-IDF network interface pointer
    TaskHandle_t _dnsTaskHandle  = nullptr;             ///< FreeRTOS Task handle for captive portal DNS server
    int          _dnsSocket      = -1;                  ///< UDP socket descriptor for DNS listener

    esp_err_t loadCredentialsFromNvs();
    esp_err_t saveCredentialsToNvs();
    esp_err_t startSoftAp();
    esp_err_t startMdns();
    esp_err_t startDnsServer();

    static void dnsTask(void* pvParameters);
};

} // namespace web

