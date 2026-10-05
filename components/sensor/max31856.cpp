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
 * @file max31856.cpp
 * @brief Implementation of MAX31856 thermocouple hardware driver.
 *
 * Implements SPI register communication, 19-bit thermocouple & 14-bit cold junction
 * temperature conversion, fault detection, and digital filtering.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "sensor/max31856.hpp"
#include "esp_log.h"
#include <cmath>
#include <algorithm>
#include <cstring>

static const char* TAG = "MAX31856";

// MAX31856 Register Map Addresses
static constexpr uint8_t REG_CR0       = 0x00;
static constexpr uint8_t REG_CR1       = 0x01;
static constexpr uint8_t REG_MASK      = 0x02;
static constexpr uint8_t REG_CJHF      = 0x03;
static constexpr uint8_t REG_CJLF      = 0x04;
static constexpr uint8_t REG_LTHFTH    = 0x05;
static constexpr uint8_t REG_LTHFTL    = 0x06;
static constexpr uint8_t REG_LTLFTH    = 0x07;
static constexpr uint8_t REG_LTLFTL    = 0x08;
static constexpr uint8_t REG_CJTO      = 0x09;
static constexpr uint8_t REG_CJTH      = 0x0A;
static constexpr uint8_t REG_CJTL      = 0x0B;
static constexpr uint8_t REG_LTCBH     = 0x0C;
static constexpr uint8_t REG_LTCBM     = 0x0D;
static constexpr uint8_t REG_LTCBL     = 0x0E;
static constexpr uint8_t REG_SR        = 0x0F;

static constexpr uint8_t SPI_WRITE_BIT = 0x80;
static constexpr uint8_t SPI_READ_MASK = 0x7F;

namespace sensor {

// ============================================================================
// Lifecycle & SPI Hardware Initialization
// ============================================================================

MAX31856::MAX31856(spi_host_device_t spiHost, gpio_num_t csPin, const MAX31856Config& config)
    : _spiHost(spiHost),
      _csPin(csPin),
      _config(config) {}

MAX31856::~MAX31856() {
    if (_spiHandle != nullptr) {
        spi_bus_remove_device(_spiHandle);
        _spiHandle = nullptr;
    }
}

bool MAX31856::begin() {
    spi_device_interface_config_t devCfg = {};
    devCfg.mode           = 1; // MAX31856 SPI Mode 1 (CPOL=0, CPHA=1)
    devCfg.clock_speed_hz = DEFAULT_MAX31856_SPI_SPEED_HZ;
    devCfg.spics_io_num   = _csPin;
    devCfg.queue_size     = 1;

    esp_err_t ret = spi_bus_add_device(_spiHost, &devCfg, &_spiHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device on CS pin %d: %s", static_cast<int>(_csPin), esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "SPI device added on CS pin %d", static_cast<int>(_csPin));
    return initHardware();
}

bool MAX31856::initHardware() {
    if (_spiHandle == nullptr) {
        return false;
    }

    // Configure CR0: CMODE, Open Circuit Fault Detection (0x10 = enabled), Filter (50Hz/60Hz)
    uint8_t cr0 = 0x10; // Enable Open Circuit fault check
    if (_config.mode == ConversionMode::CONTINUOUS) {
        cr0 |= (1 << 7); // CMODE = 1
    }
    if (_config.filter == NoiseFilter::FILTER_50HZ) {
        cr0 |= (1 << 0); // 50Hz rejection
    }
    writeRegister(REG_CR0, cr0);

    // Configure CR1: Averaging Mode (bits 6:4) + Thermocouple Type (bits 3:0)
    uint8_t cr1 = (static_cast<uint8_t>(_config.averaging) << 4) |
                  (static_cast<uint8_t>(_config.tcType) & 0x0F);
    writeRegister(REG_CR1, cr1);

    // Unmask non-critical fault bits
    writeRegister(REG_MASK, 0x00);

    // Write High/Low temperature threshold registers and CJTO
    writeThresholds();

    // Clear fault register on initial power-up
    clearFaultRegister();

    ESP_LOGI(TAG, "MAX31856 hardware initialized: Type=%d, Mode=%d, Filter=%d",
             static_cast<int>(_config.tcType), static_cast<int>(_config.mode), static_cast<int>(_config.filter));

    return true;
}

bool MAX31856::applyConfig(const MAX31856Config& config) {
    if (_spiHandle == nullptr) {
        _config = config;
        return false;
    }

    // 1. Update Cold Junction Offset only if value numerically changed
    if (std::fabs(_config.cjOffset - config.cjOffset) > 0.001f) {
        _config.cjOffset = config.cjOffset;
        writeCjtoOffset(_config.cjOffset);
    }

    // 2. Update software filter & fault parameters in RAM (no SPI traffic)
    _config.emaFilterEnabled = config.emaFilterEnabled;
    _config.emaAlpha         = config.emaAlpha;
    _config.faultStreakLimit = config.faultStreakLimit;

    // 3. Update hardware threshold registers only if changed
    if (_config.tcHighFaultTemp != config.tcHighFaultTemp ||
        _config.tcLowFaultTemp  != config.tcLowFaultTemp  ||
        _config.cjHighFaultTemp != config.cjHighFaultTemp ||
        _config.cjLowFaultTemp  != config.cjLowFaultTemp) {
        _config.tcHighFaultTemp = config.tcHighFaultTemp;
        _config.tcLowFaultTemp  = config.tcLowFaultTemp;
        _config.cjHighFaultTemp = config.cjHighFaultTemp;
        _config.cjLowFaultTemp  = config.cjLowFaultTemp;
        writeThresholds();
    }

    // 4. Update CR0 / CR1 only if operating mode or TC type changed
    if (_config.tcType != config.tcType || _config.filter != config.filter ||
        _config.averaging != config.averaging || _config.mode != config.mode) {
        _config.tcType    = config.tcType;
        _config.filter    = config.filter;
        _config.averaging = config.averaging;
        _config.mode      = config.mode;

        uint8_t cr0 = 0x10;
        if (_config.mode == ConversionMode::CONTINUOUS) cr0 |= (1 << 7);
        if (_config.filter == NoiseFilter::FILTER_50HZ) cr0 |= (1 << 0);
        writeRegister(REG_CR0, cr0);

        uint8_t cr1 = (static_cast<uint8_t>(_config.averaging) << 4) |
                      (static_cast<uint8_t>(_config.tcType) & 0x0F);
        writeRegister(REG_CR1, cr1);
    }

    ESP_LOGI(TAG, "MAX31856 runtime config updated: CJTO=%.2f°C, EMA_alpha=%.2f, faultStreakLimit=%d",
             _config.cjOffset, _config.emaAlpha, (int)_config.faultStreakLimit);
    return true;
}

void MAX31856::writeCjtoOffset(float offset) {
    float clampedOffset = std::clamp(offset, -8.0f, 7.9375f);
    int8_t cjtoRaw = static_cast<int8_t>(std::round(clampedOffset * 16.0f));
    writeRegister(REG_CJTO, static_cast<uint8_t>(cjtoRaw));
}

void MAX31856::writeThresholds() {
    // Cold Junction High / Low thresholds
    writeRegister(REG_CJHF, static_cast<uint8_t>(static_cast<int8_t>(_config.cjHighFaultTemp)));
    writeRegister(REG_CJLF, static_cast<uint8_t>(static_cast<int8_t>(_config.cjLowFaultTemp)));

    // Cold Junction Temperature Offset (CJTO 0x09: signed 8-bit, LSB = 0.0625°C, range -8°C to +7.9375°C)
    writeCjtoOffset(_config.cjOffset);

    // Thermocouple High threshold (16-bit signed, LSB = 0.0625°C)
    int16_t tcHighRaw = static_cast<int16_t>(_config.tcHighFaultTemp * 16.0f);
    writeRegister(REG_LTHFTH, static_cast<uint8_t>((tcHighRaw >> 8) & 0xFF));
    writeRegister(REG_LTHFTL, static_cast<uint8_t>(tcHighRaw & 0xFF));

    // Thermocouple Low threshold (16-bit signed, LSB = 0.0625°C)
    int16_t tcLowRaw = static_cast<int16_t>(_config.tcLowFaultTemp * 16.0f);
    writeRegister(REG_LTLFTH, static_cast<uint8_t>((tcLowRaw >> 8) & 0xFF));
    writeRegister(REG_LTLFTL, static_cast<uint8_t>(tcLowRaw & 0xFF));
}

SensorReading MAX31856::read() {
    _latestReading.timestampMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    if (_spiHandle == nullptr) {
        _latestReading.isValid = false;
        return _latestReading;
    }

    // Single atomic burst read of Cold Junction (0x0A-0x0B), Linearized TC (0x0C-0x0E), and Fault SR (0x0F)
    uint8_t burstBuf[6] = {0};
    if (!readRegisters(REG_CJTH, burstBuf, sizeof(burstBuf))) {
        _latestReading.isValid = false;
        return _latestReading;
    }

    uint8_t cjHigh   = burstBuf[0]; // 0x0A: CJTH
    uint8_t cjLow    = burstBuf[1]; // 0x0B: CJTL
    uint8_t ltcHigh  = burstBuf[2]; // 0x0C: LTCBH
    uint8_t ltcMid   = burstBuf[3]; // 0x0D: LTCBM
    uint8_t ltcLow   = burstBuf[4]; // 0x0E: LTCBL
    uint8_t rawFault = burstBuf[5]; // 0x0F: SR

    parseFaultRegister(rawFault, _latestReading.fault);

    if (rawFault != 0) {
        // Clear latched fault register so next conversion can recover if fault was transient
        clearFaultRegister();

        _faultStreak++;
        if (_faultStreak >= _config.faultStreakLimit) {
            _latestReading.isValid = false;
            ESP_LOGW(TAG, "MAX31856 CS pin %d fault verified (streak %d/%d): 0x%02X",
                     static_cast<int>(_csPin), _faultStreak, _config.faultStreakLimit, rawFault);
            return _latestReading;
        }
    } else {
        _faultStreak = 0;
    }

    // Thermocouple Temperature (19-bit signed in bits 23..5)
    int32_t rawTc = (static_cast<int32_t>(ltcHigh) << 16) |
                    (static_cast<int32_t>(ltcMid)  << 8)  |
                     static_cast<int32_t>(ltcLow);
    if (rawTc & 0x800000) {
        rawTc |= 0xFF000000; // Sign extend 24-bit to 32-bit signed int
    }
    float rawTemp = static_cast<float>(rawTc >> 5) * 0.0078125f; // LSB = 0.0078125°C (1/128°C)

    // Cold Junction Temperature (14-bit signed in bits 15..2)
    int16_t rawCj = (static_cast<int16_t>(cjHigh) << 8) | static_cast<int16_t>(cjLow);
    float coldJunctionTemp = static_cast<float>(rawCj >> 2) * 0.015625f; // LSB = 0.015625°C (1/64°C)

    // Check sanity limits (reject obvious open/short SPI noise)
    if (std::isnan(rawTemp) || rawTemp < -100.0f || rawTemp > 1850.0f) {
        _latestReading.isValid = false;
        return _latestReading;
    }

    _latestReading.rawTemperature = rawTemp;
    _latestReading.coldJunction   = coldJunctionTemp;
    _latestReading.isValid        = true;

    // Apply EMA filter
    if (_config.emaFilterEnabled) {
        if (!_filterInit) {
            _filteredTemp = rawTemp;
            _filterInit   = true;
        } else {
            _filteredTemp = (_config.emaAlpha * rawTemp) + ((1.0f - _config.emaAlpha) * _filteredTemp);
        }
        _latestReading.temperature = _filteredTemp;
    } else {
        _latestReading.temperature = rawTemp;
        _filteredTemp              = rawTemp;
    }

    return _latestReading;
}

void MAX31856::parseFaultRegister(uint8_t rawFault, FaultFlags& flags) {
    flags.rawByte          = rawFault;
    flags.cjOutOfRange     = (rawFault & (1 << 7)) != 0;
    flags.tcOutOfRange     = (rawFault & (1 << 6)) != 0;
    flags.cjHigh           = (rawFault & (1 << 5)) != 0;
    flags.cjLow            = (rawFault & (1 << 4)) != 0;
    flags.tcHigh           = (rawFault & (1 << 3)) != 0;
    flags.tcLow            = (rawFault & (1 << 2)) != 0;
    flags.overUnderVoltage = (rawFault & (1 << 1)) != 0;
    flags.openCircuit      = (rawFault & (1 << 0)) != 0;
}

void MAX31856::resetFilter(float initialTemp) {
    _filteredTemp                 = initialTemp;
    _filterInit                   = true;
    _latestReading.temperature    = initialTemp;
    _latestReading.rawTemperature = initialTemp;
}

bool MAX31856::triggerOneShot() {
    uint8_t cr0 = readRegister(REG_CR0);
    cr0 |= (1 << 6); // Set 1SHOT bit
    return writeRegister(REG_CR0, cr0);
}

void MAX31856::clearFaultRegister() {
    uint8_t cr0 = readRegister(REG_CR0);
    cr0 |= (1 << 1); // Set FAULTCLR bit
    writeRegister(REG_CR0, cr0);
}

uint8_t MAX31856::readRegister(uint8_t regAddr) {
    if (_spiHandle == nullptr) return 0;
    uint8_t tx[2] = { static_cast<uint8_t>(regAddr & SPI_READ_MASK), 0x00 };
    uint8_t rx[2] = { 0, 0 };
    spi_transaction_t t = {};
    t.length    = 16;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_transmit(_spiHandle, &t);
    return rx[1];
}

// ============================================================================
// Low-Level SPI Register Transactions
// ============================================================================

bool MAX31856::readRegisters(uint8_t startReg, uint8_t* buffer, size_t length) {
    if (_spiHandle == nullptr || buffer == nullptr || length == 0) return false;
    if (length > 16) return false;

    uint8_t tx[17] = {0};
    uint8_t rx[17] = {0};
    tx[0] = static_cast<uint8_t>(startReg & SPI_READ_MASK);

    spi_transaction_t t = {};
    t.length    = static_cast<size_t>((length + 1) * 8);
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    esp_err_t ret = spi_device_transmit(_spiHandle, &t);
    if (ret != ESP_OK) return false;

    std::memcpy(buffer, &rx[1], length);
    return true;
}

bool MAX31856::writeRegister(uint8_t regAddr, uint8_t value) {
    if (_spiHandle == nullptr) return false;
    uint8_t tx[2] = { static_cast<uint8_t>(regAddr | SPI_WRITE_BIT), value };
    spi_transaction_t t = {};
    t.length    = 16;
    t.tx_buffer = tx;
    esp_err_t ret = spi_device_transmit(_spiHandle, &t);
    return (ret == ESP_OK);
}

} // namespace sensor
