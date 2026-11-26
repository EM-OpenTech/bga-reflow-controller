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
 * @file usb_network.cpp
 * @brief Implementation of USB-OTG Virtual Ethernet Network Manager.
 *
 * Configures TinyUSB CDC-NCM network class, binds to esp_netif,
 * and configures the DHCP server on the 192.168.5.1/24 subnet.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/usb_network.hpp"
#include "esp_log.h"
#include <array>
#include <cstring>

#ifdef ESP_PLATFORM
#include "dhcpserver/dhcpserver.h"
#include "dhcpserver/dhcpserver_options.h"
#include "esp_mac.h"
#include "lwip/esp_netif_net_stack.h"
#include "mdns.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_net.h"
#endif

namespace web {

namespace {

constexpr const char* TAG = "UsbNetwork";

#ifdef ESP_PLATFORM
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
esp_netif_t* s_activeNetif = nullptr;

void l2FreeCallback(void* /*h*/, void* buffer) {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    free(buffer);
}

esp_err_t netifTransmitCallback(void* /*h*/, void* buffer, size_t len) {
    return tinyusb_net_send_sync(buffer, static_cast<uint16_t>(len), nullptr, pdMS_TO_TICKS(100));
}

esp_err_t usbRecvCallback(void* buffer, uint16_t len, void* /*ctx*/) {
    if (s_activeNetif != nullptr && buffer != nullptr && len > 0) {
        // Raw buffer required by esp_netif_receive C-API which takes ownership of the memory
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
        void* bufCopy = malloc(len);
        if (bufCopy == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(bufCopy, buffer, len);
        return esp_netif_receive(s_activeNetif, bufCopy, len, nullptr);
    }
    return ESP_OK;
}
#endif

} // namespace

UsbNetwork& UsbNetwork::instance() {
    static UsbNetwork s_instance;
    return s_instance;
}

esp_err_t UsbNetwork::begin() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_initialized) {
        return ESP_OK;
    }

#ifdef ESP_PLATFORM
    esp_err_t err = initUsbNetif();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB Network interface: %s", esp_err_to_name(err));
        return err;
    }
#endif

    _initialized = true;
    ESP_LOGI(TAG, "USB-OTG Virtual Ethernet active at http://%s (reflow.local)", DEFAULT_USB_NET_IP);
    return ESP_OK;
}

void UsbNetwork::stop() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_initialized) {
        return;
    }

#ifdef ESP_PLATFORM
    if (_usbNetif != nullptr) {
        mdns_netif_action(_usbNetif, MDNS_EVENT_DISABLE_IP4);
        mdns_unregister_netif(_usbNetif);
        esp_netif_action_stop(_usbNetif, nullptr, 0, nullptr);
        esp_netif_destroy(_usbNetif);
        _usbNetif = nullptr;
        s_activeNetif = nullptr;
    }
#endif

    _initialized = false;
    ESP_LOGI(TAG, "USB Network interface stopped.");
}

std::string UsbNetwork::getIpAddress() const {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_initialized) {
        return "0.0.0.0";
    }
    return DEFAULT_USB_NET_IP;
}

esp_err_t UsbNetwork::initUsbNetif() {
#ifdef ESP_PLATFORM
    // 1. Install TinyUSB stack with default configuration
    const tinyusb_config_t tusbCfg = TINYUSB_DEFAULT_CONFIG();
    esp_err_t err = tinyusb_driver_install(&tusbCfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install TinyUSB driver: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Initialize TinyUSB Network Device with local MAC
    tinyusb_net_config_t netConfig{};
    netConfig.mac_addr[0] = 0x02;
    netConfig.mac_addr[1] = 0x02;
    netConfig.mac_addr[2] = 0x11;
    netConfig.mac_addr[3] = 0x22;
    netConfig.mac_addr[4] = 0x33;
    netConfig.mac_addr[5] = 0x01;
    netConfig.on_recv_callback = usbRecvCallback;
    netConfig.free_tx_buffer = nullptr;
    netConfig.on_init_callback = nullptr;
    netConfig.user_context = nullptr;

    err = tinyusb_net_init(&netConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init TinyUSB Net: %s", esp_err_to_name(err));
        return err;
    }

    // 3. Define USB-OTG Subnet IP Info (192.168.5.1/24)
    esp_netif_ip_info_t ipInfo{};
    ipInfo.ip.addr = esp_ip4addr_aton(DEFAULT_USB_NET_IP);
    ipInfo.netmask.addr = esp_ip4addr_aton(DEFAULT_USB_NET_MASK);
    ipInfo.gw.addr = esp_ip4addr_aton(DEFAULT_USB_NET_GW);

    // 4. Configure base netif (DHCP server active, Auto-up)
    esp_netif_inherent_config_t baseCfg{};
    baseCfg.flags = static_cast<esp_netif_flags_t>(ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP);
    baseCfg.ip_info = &ipInfo;
    baseCfg.if_key = "usb_net";
    baseCfg.if_desc = "USB-OTG Ethernet Interface";
    baseCfg.route_prio = 10;
    baseCfg.bridge_info = nullptr;

    // 5. Driver configuration
    esp_netif_driver_ifconfig_t driverCfg{};
    driverCfg.handle = this; // Type-safe driver instance handle (!= nullptr)
    driverCfg.transmit = netifTransmitCallback;
    driverCfg.transmit_wrap = nullptr;
    driverCfg.driver_free_rx_buffer = l2FreeCallback;

    // 6. lwIP Ethernet Stack integration
    struct esp_netif_netstack_config lwipNetifConfig = {
        .lwip =
            {
                .init_fn = ethernetif_init,
                .input_fn = ethernetif_input,
            },
    };

    esp_netif_config_t cfg = {
        .base = &baseCfg,
        .driver = &driverCfg,
        .stack = &lwipNetifConfig,
    };

    _usbNetif = esp_netif_new(&cfg);
    if (_usbNetif == nullptr) {
        ESP_LOGE(TAG, "Failed to create esp_netif for USB");
        return ESP_FAIL;
    }

    std::array<uint8_t, 6> lwipMac = {0x02, 0x02, 0x11, 0x22, 0x33, 0x02};
    esp_netif_set_mac(_usbNetif, lwipMac.data());

    // Set minimum DHCP lease time
    uint32_t leaseOpt = 1;
    esp_netif_dhcps_option(_usbNetif, ESP_NETIF_OP_SET, ESP_NETIF_IP_ADDRESS_LEASE_TIME, &leaseOpt, sizeof(leaseOpt));

    // Disable offering Default Gateway (DHCP Option 3 - Router) and DNS (Option 6).
    // Informs host OS (macOS, Android, Windows) that this USB interface has no internet gateway
    // or DNS resolver, keeping Wi-Fi/Cellular active as default route and DNS while routing 192.168.5.x locally.
    uint8_t dhcpOffer = 0;
    esp_netif_dhcps_option(_usbNetif, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &dhcpOffer,
                           sizeof(dhcpOffer));
    uint8_t dnsOffer = 0;
    esp_netif_dhcps_option(_usbNetif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dnsOffer, sizeof(dnsOffer));

    s_activeNetif = _usbNetif;
    esp_netif_action_start(_usbNetif, nullptr, 0, nullptr);

    // 7. Register custom USB netif with mDNS so reflow.local resolves to 192.168.5.1 over USB
    const esp_err_t mdnsErr = mdns_register_netif(_usbNetif);
    if (mdnsErr != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register USB netif in mDNS: %s", esp_err_to_name(mdnsErr));
    } else {
        mdns_netif_action(_usbNetif,
                          static_cast<mdns_event_actions_t>(MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4));
    }
#endif
    return ESP_OK;
}

} // namespace web
