// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include <cstring>
#include <stddef.h>
#include <stdint.h>

typedef void* spi_device_handle_t;
typedef void* spi_host_device_t;
#define SPI2_HOST ((spi_host_device_t)(uintptr_t)1)

typedef struct {
    uint32_t flags;
    size_t length;
    size_t rxlength;
    const void* tx_buffer;
    void* rx_buffer;
} spi_transaction_t;

typedef struct {
    int mosi_io_num;
    int miso_io_num;
    int sclk_io_num;
    int quadwp_io_num;
    int quadhd_io_num;
    int max_transfer_sz;
} spi_bus_config_t;

typedef struct {
    int mode;
    int clock_speed_hz;
    int spics_io_num;
    uint32_t flags;
    int queue_size;
    int cs_ena_pretrans;
    int cs_ena_posttrans;
    int input_delay_ns;
} spi_device_interface_config_t;

// ============================================================================
// Simulated MAX31856 SPI Register Bank (Host Testbed)
// ============================================================================

inline bool g_mockSpiFailAddDevice = false;
inline bool g_mockSpiFailTransmit = false;
inline int g_mockSpiFailOnReg = -1;
inline int g_mockSpiCorruptReadReg = -1;
inline uint8_t g_mockSpiCorruptVal = 0x00;

inline uint8_t g_mockSpiRegisters[16] = {
    0x90, // 0x00: CR0 (CMODE=1, OCFAULT=01)
    0x03, // 0x01: CR1 (Type K, 1x)
    0x00, // 0x02: MASK (all unmasked)
    0x55, // 0x03: CJHF
    0xEC, // 0x04: CJLF
    0x12, // 0x05: LTHFTH
    0xC0, // 0x06: LTHFTL
    0xFF, // 0x07: LTLFTH
    0x60, // 0x08: LTLFTL
    0x00, // 0x09: CJTO
    0x06, // 0x0A: CJTH (25°C)
    0x40, // 0x0B: CJTL
    0x00, // 0x0C: LTCBH (0°C)
    0x00, // 0x0D: LTCBM
    0x00, // 0x0E: LTCBL
    0x00  // 0x0F: SR (no faults)
};

inline void mockSpiResetRegisters() {
    g_mockSpiFailAddDevice = false;
    g_mockSpiFailTransmit = false;
    g_mockSpiFailOnReg = -1;
    g_mockSpiCorruptReadReg = -1;
    g_mockSpiCorruptVal = 0x00;
    g_mockSpiRegisters[0] = 0x90;  // CR0
    g_mockSpiRegisters[1] = 0x03;  // CR1
    g_mockSpiRegisters[2] = 0x00;  // MASK
    g_mockSpiRegisters[3] = 0x55;  // CJHF
    g_mockSpiRegisters[4] = 0xEC;  // CJLF
    g_mockSpiRegisters[5] = 0x12;  // LTHFTH
    g_mockSpiRegisters[6] = 0xC0;  // LTHFTL
    g_mockSpiRegisters[7] = 0xFF;  // LTLFTH
    g_mockSpiRegisters[8] = 0x60;  // LTLFTL
    g_mockSpiRegisters[9] = 0x00;  // CJTO
    g_mockSpiRegisters[10] = 0x06; // CJTH
    g_mockSpiRegisters[11] = 0x40; // CJTL
    g_mockSpiRegisters[12] = 0x00; // LTCBH
    g_mockSpiRegisters[13] = 0x00; // LTCBM
    g_mockSpiRegisters[14] = 0x00; // LTCBL
    g_mockSpiRegisters[15] = 0x00; // SR
}

inline void mockSpiSetTemperature(float tempC, float cjTempC = 25.0f) {
    // TC 19-bit encoding: LSB = 0.0078125°C (1/128)
    int32_t rawTc = static_cast<int32_t>(tempC * 128.0f) << 5;
    g_mockSpiRegisters[12] = static_cast<uint8_t>((rawTc >> 16) & 0xFF);
    g_mockSpiRegisters[13] = static_cast<uint8_t>((rawTc >> 8) & 0xFF);
    g_mockSpiRegisters[14] = static_cast<uint8_t>(rawTc & 0xE0);

    // CJ 14-bit encoding: LSB = 0.015625°C (1/64)
    int16_t rawCj = static_cast<int16_t>(cjTempC * 64.0f) << 2;
    g_mockSpiRegisters[10] = static_cast<uint8_t>((rawCj >> 8) & 0xFF);
    g_mockSpiRegisters[11] = static_cast<uint8_t>(rawCj & 0xFC);
}

inline esp_err_t spi_bus_initialize(spi_host_device_t, const spi_bus_config_t*, int) {
    return ESP_OK;
}
inline esp_err_t spi_bus_add_device(spi_host_device_t, const spi_device_interface_config_t*, spi_device_handle_t* out) {
    if (g_mockSpiFailAddDevice)
        return ESP_FAIL;
    if (out)
        *out = (spi_device_handle_t)(uintptr_t)0x1234;
    return ESP_OK;
}

inline esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t* trans) {
    if (g_mockSpiFailTransmit)
        return ESP_FAIL;
    if (!trans)
        return ESP_OK;
    const auto* tx = static_cast<const uint8_t*>(trans->tx_buffer);
    auto* rx = static_cast<uint8_t*>(trans->rx_buffer);
    if (!tx)
        return ESP_OK;

    size_t byteCount = (trans->length + 7) / 8;
    if (byteCount == 0)
        return ESP_OK;

    uint8_t cmdByte = tx[0];
    bool isWrite = ((cmdByte & 0x80) != 0);
    uint8_t regAddr = cmdByte & 0x0F;

    if (g_mockSpiFailOnReg >= 0 && (regAddr == static_cast<uint8_t>(g_mockSpiFailOnReg))) {
        return ESP_FAIL;
    }

    if (isWrite) {
        for (size_t i = 1; i < byteCount; ++i) {
            uint8_t target = static_cast<uint8_t>((regAddr + (i - 1)) & 0x0F);
            g_mockSpiRegisters[target] = tx[i];
        }
    } else {
        if (rx) {
            rx[0] = 0xFF; // dummy
            for (size_t i = 1; i < byteCount; ++i) {
                uint8_t target = static_cast<uint8_t>((regAddr + (i - 1)) & 0x0F);
                if (g_mockSpiCorruptReadReg >= 0 && target == static_cast<uint8_t>(g_mockSpiCorruptReadReg)) {
                    rx[i] = g_mockSpiCorruptVal;
                } else {
                    rx[i] = g_mockSpiRegisters[target];
                }
            }
        }
    }
    return ESP_OK;
}

inline esp_err_t spi_bus_remove_device(spi_device_handle_t) {
    return ESP_OK;
}
inline esp_err_t spi_bus_free(spi_host_device_t) {
    return ESP_OK;
}
