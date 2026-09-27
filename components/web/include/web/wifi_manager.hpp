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
 * @brief Manages SoftAP Wi-Fi, NVS credentials storage, mDNS, and Captive Portal DNS.
 * 
 * Features:
 * - Native ESP-IDF v6.0.2 esp_wifi / esp_netif implementation.
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
    std::string _ssid            = DEFAULT_AP_SSID;
    std::string _password        = DEFAULT_AP_PASSWORD;
    bool        _passwordChanged = false;

    esp_netif_t* _apNetif        = nullptr;
    TaskHandle_t _dnsTaskHandle  = nullptr;
    int          _dnsSocket      = -1;

    esp_err_t loadCredentialsFromNvs();
    esp_err_t saveCredentialsToNvs();
    esp_err_t startSoftAp();
    esp_err_t startMdns();
    esp_err_t startDnsServer();

    static void dnsTask(void* pvParameters);
};

} // namespace web
