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
 * @file usb_network.hpp
 * @brief USB-OTG Virtual Ethernet Network Manager (CDC-NCM).
 *
 * Coordinates USB-OTG Ethernet interface configuration, DHCP server instantiation,
 * and direct browser dashboard access over USB without Wi-Fi.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "esp_err.h"
#include "esp_netif.h"
#include <cstdint>
#include <mutex>
#include <string>

namespace web {

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

/// Default USB-OTG Subnet IP Address (Gateway for Host PC)
constexpr const char* DEFAULT_USB_NET_IP = "192.168.5.1";

/// Default USB-OTG Subnet Netmask
constexpr const char* DEFAULT_USB_NET_MASK = "255.255.255.0";

/// Default USB-OTG Gateway Address
constexpr const char* DEFAULT_USB_NET_GW = "192.168.5.1";

/**
 * @class UsbNetwork
 * @brief Singleton manager for USB-OTG Virtual Ethernet Network interface.
 */
class UsbNetwork {
public:
    /**
     * @brief Access the singleton UsbNetwork instance.
     *
     * @return UsbNetwork& Reference to global singleton.
     */
    static UsbNetwork& instance();

    // Deleted copy/move semantics
    UsbNetwork(const UsbNetwork&) = delete;
    UsbNetwork& operator=(const UsbNetwork&) = delete;
    UsbNetwork(UsbNetwork&&) = delete;
    UsbNetwork& operator=(UsbNetwork&&) = delete;

    /**
     * @brief Initialize TinyUSB stack, create USB network netif, and start DHCP server.
     *
     * @return esp_err_t ESP_OK on success, or appropriate error code.
     */
    esp_err_t begin();

    /**
     * @brief Stop USB network interface and uninstall driver.
     */
    void stop();

    /**
     * @brief Check whether USB network interface is initialized and running.
     *
     * @return true if initialized, false otherwise.
     */
    [[nodiscard]] bool isInitialized() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _initialized;
    }

    /**
     * @brief Get active USB interface IP address string.
     *
     * @return std::string IP address (e.g. "192.168.5.1").
     */
    [[nodiscard]] std::string getIpAddress() const;

private:
    UsbNetwork() = default;
    ~UsbNetwork() = default;

    mutable std::mutex _mutex;
    bool _initialized = false;
    esp_netif_t* _usbNetif = nullptr;

    esp_err_t initUsbNetif();
};

} // namespace web
