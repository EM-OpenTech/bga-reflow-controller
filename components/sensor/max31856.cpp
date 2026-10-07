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

bool MAX31856::begin(bool skipPriming) {
    spi_device_interface_config_t devCfg = {};
    devCfg.mode           = 1;  // MAX31856 SPI Mode 1 (CPOL=0, CPHA=1)
    devCfg.clock_speed_hz = DEFAULT_MAX31856_SPI_SPEED_HZ;
    devCfg.spics_io_num   = _csPin;
    devCfg.queue_size     = 1;
    // cs_ena_pretrans is intentionally NOT set:
    // ESP-IDF hardware limitation — cs_ena_pretrans is silently ignored in full-duplex
    // mode (ESP32 SPI peripheral silicon limitation). At 200 kHz the natural bus timing
    // provides far more than the required 100 ns (tCC). cs_ena_posttrans works correctly
    // in both full-duplex and half-duplex.
    devCfg.cs_ena_posttrans = 4; // 4 SPI bit-cycles ≈ 20 µs @ 200 kHz >> 400 ns tCWH
    devCfg.input_delay_ns   = 80; // MAX31856 tCDD (SCLK fall → MISO valid) = 80 ns max

    esp_err_t ret = spi_bus_add_device(_spiHost, &devCfg, &_spiHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device on CS pin %d: %s", static_cast<int>(_csPin), esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "SPI device added on CS pin %d (Speed: %u Hz, CS posttrans: 4 cycles, input_delay: 80ns)",
             static_cast<int>(_csPin), static_cast<unsigned>(DEFAULT_MAX31856_SPI_SPEED_HZ));

    if (!initHardware()) {
        return false;
    }

    if (skipPriming) {
        // Caller is responsible for waiting conversionTimeMs() before the first read().
        // Use this for parallel multi-sensor init to avoid N × blockingDelay overhead.
        // See begin() header docstring for the recommended pattern.
        ESP_LOGI(TAG, "CS pin %d: priming skipped — caller must wait >= %u ms before read()",
                 static_cast<int>(_csPin), conversionTimeMs());
        return true;
    }

    // Startup Sensor Priming:
    // In continuous mode the MAX31856 requires a full first-conversion cycle before
    // the result registers hold valid data. The FIRST conversion is longer than subsequent
    // ones (MAX31856 datasheet Table 2). Compute the exact delay from the active config,
    // then add a 50 ms safety margin.
    // SAMPLES_1 @ 50Hz:  169 ms + 50 ms = 219 ms
    // SAMPLES_4 @ 50Hz:  289 ms + 50 ms = 339 ms  (was incorrect 200 ms)
    // SAMPLES_16 @ 50Hz: 769 ms + 50 ms = 819 ms
    //
    // Note: This blocks the calling task. For multiple sensors, use begin(true) on all
    // sensors first, wait once, then call read() on each. See header docstring.
    if (_config.mode == ConversionMode::CONTINUOUS) {
        uint32_t primeMs = conversionTimeMs() + 50u;
        ESP_LOGI(TAG, "Waiting %u ms for first conversion (SAMPLES=%d, %sHz filter)...",
                 primeMs,
                 1 << static_cast<int>(_config.averaging),
                 _config.filter == NoiseFilter::FILTER_50HZ ? "50" : "60");
        vTaskDelay(pdMS_TO_TICKS(primeMs));
        read();
    }

    return true;
}

bool MAX31856::initHardware() {
    if (_spiHandle == nullptr) {
        return false;
    }

    // Step 1: Place MAX31856 into 'Normally Off' mode (CMODE=0) during configuration.
    // Datasheet Note (p. 19) & Linux kernel driver: Changing TC type, averaging, or filter notch
    // frequencies must occur while CMODE=0 to avoid internal DSP / filter state corruption.
    writeRegister(REG_CR0, 0x10); // CMODE=0, Open Circuit check enabled

    // Step 2: Configure CR1: Averaging Mode (bits 6:4) + Thermocouple Type (bits 3:0)
    uint8_t cr1 = (static_cast<uint8_t>(_config.averaging) << 4) |
                  (static_cast<uint8_t>(_config.tcType) & 0x0F);
    writeRegister(REG_CR1, cr1);

    // Step 3: Configure Fault Mask Register (0x02 / MASK):
    // Datasheet power-on default is 0xFF (all fault bits masked → FAULT pin never asserts).
    // Writing 0x00 unmasks all fault conditions, enabling the hardware FAULT pin for any fault.
    // This allows external hardware fault detection independent of software polling.
    // Even with mask=0xFF, all faults are still readable in the SR register (0x0F).
    writeRegister(REG_MASK, 0x00);

    // Step 4: Write High/Low temperature threshold registers and CJTO
    writeThresholds();

    // Step 5: Activate target conversion mode and 50Hz/60Hz filter in CR0
    uint8_t cr0 = 0x10; // Open Circuit check enabled
    if (_config.mode == ConversionMode::CONTINUOUS) {
        cr0 |= (1 << 7); // CMODE = 1
    }
    if (_config.filter == NoiseFilter::FILTER_50HZ) {
        cr0 |= (1 << 0); // 50Hz rejection
    }
    writeRegister(REG_CR0, cr0);

    // Step 6: Verify configuration via readback comparison
    float clampedOffset = std::clamp(_config.cjOffset, -8.0f, 7.9375f);
    int8_t expectedCjto = static_cast<int8_t>(std::round(clampedOffset * 16.0f));

    if (!verifyConfig(cr0, cr1, expectedCjto)) {
        return false;
    }

    ESP_LOGI(TAG, "MAX31856 hardware initialized: CS=%d, Type=%d, Mode=%d, Filter=%d",
             static_cast<int>(_csPin), static_cast<int>(_config.tcType),
             static_cast<int>(_config.mode), static_cast<int>(_config.filter));

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
    // Reset the fault streak counter whenever the limit threshold changes.
    // Without this, an existing streak accumulated under the old limit could
    // immediately exceed a newly lowered limit and falsely declare a fault.
    if (_config.faultStreakLimit != config.faultStreakLimit) {
        _config.faultStreakLimit = config.faultStreakLimit;
        _faultStreak = 0;
    }

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

        // Temporarily pause conversion (CMODE=0) while updating notch filter and TC type
        writeRegister(REG_CR0, 0x10);

        uint8_t cr1 = (static_cast<uint8_t>(_config.averaging) << 4) |
                      (static_cast<uint8_t>(_config.tcType) & 0x0F);
        writeRegister(REG_CR1, cr1);

        uint8_t cr0 = 0x10;
        if (_config.mode == ConversionMode::CONTINUOUS) cr0 |= (1 << 7);
        if (_config.filter == NoiseFilter::FILTER_50HZ) cr0 |= (1 << 0);
        writeRegister(REG_CR0, cr0);
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
    // Cold Junction High / Low thresholds (REG_CJHF 0x03, REG_CJLF 0x04)
    // Datasheet: signed 8-bit integer, LSB = 1°C, range -128°C to +127°C.
    // Bug fix: The original cast truncated toward zero (e.g., 85.9°C → 85) and
    //   was undefined behavior for float values outside [-128, 127]. Using
    //   std::round() + std::clamp() prevents both issues.
    auto encodeCjThreshold = [](float tempC) -> uint8_t {
        float clamped = std::clamp(tempC, -128.0f, 127.0f);
        return static_cast<uint8_t>(static_cast<int8_t>(std::round(clamped)));
    };
    writeRegister(REG_CJHF, encodeCjThreshold(_config.cjHighFaultTemp));
    writeRegister(REG_CJLF, encodeCjThreshold(_config.cjLowFaultTemp));

    // Cold Junction Temperature Offset (CJTO 0x09: signed 8-bit, LSB = 0.0625°C, range -8°C to +7.9375°C)
    writeCjtoOffset(_config.cjOffset);

    // Thermocouple High threshold (LTHFTH/LTHFTL 0x05/0x06: 16-bit signed, LSB = 0.0625°C)
    // Range: [-2048°C, +2047.9375°C]. Clamp to prevent int16_t overflow UB.
    float tcHighClamped = std::clamp(_config.tcHighFaultTemp, -2048.0f, 2047.9375f);
    int16_t tcHighRaw = static_cast<int16_t>(std::round(tcHighClamped * 16.0f));
    writeRegister(REG_LTHFTH, static_cast<uint8_t>((tcHighRaw >> 8) & 0xFF));
    writeRegister(REG_LTHFTL, static_cast<uint8_t>(tcHighRaw & 0xFF));

    // Thermocouple Low threshold (LTLFTH/LTLFTL 0x07/0x08: 16-bit signed, LSB = 0.0625°C)
    float tcLowClamped = std::clamp(_config.tcLowFaultTemp, -2048.0f, 2047.9375f);
    int16_t tcLowRaw = static_cast<int16_t>(std::round(tcLowClamped * 16.0f));
    writeRegister(REG_LTLFTH, static_cast<uint8_t>((tcLowRaw >> 8) & 0xFF));
    writeRegister(REG_LTLFTL, static_cast<uint8_t>(tcLowRaw & 0xFF));
}

SensorReading MAX31856::getLatest() const {
    SensorReading r;
    portENTER_CRITICAL(&_mux);
    r = _latestReading;
    portEXIT_CRITICAL(&_mux);
    return r;
}

SensorReading MAX31856::read() {
    SensorReading localReading;
    localReading.timestampMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    if (_spiHandle == nullptr) {
        localReading.isValid = false;
        portENTER_CRITICAL(&_mux);
        _latestReading = localReading;
        portEXIT_CRITICAL(&_mux);
        return localReading;
    }

    // Single atomic burst read of Cold Junction (0x0A-0x0B), Linearized TC (0x0C-0x0E), and Fault SR (0x0F)
    uint8_t burstBuf[6] = {0};
    if (!readRegisters(REG_CJTH, burstBuf, sizeof(burstBuf))) {
        localReading.isValid = false;
        portENTER_CRITICAL(&_mux);
        _latestReading = localReading;
        portEXIT_CRITICAL(&_mux);
        return localReading;
    }

    uint8_t cjHigh   = burstBuf[0]; // 0x0A: CJTH
    uint8_t cjLow    = burstBuf[1]; // 0x0B: CJTL
    uint8_t ltcHigh  = burstBuf[2]; // 0x0C: LTCBH
    uint8_t ltcMid   = burstBuf[3]; // 0x0D: LTCBM
    uint8_t ltcLow   = burstBuf[4]; // 0x0E: LTCBL
    uint8_t rawFault = burstBuf[5]; // 0x0F: SR

    parseFaultRegister(rawFault, localReading.fault);

    if (rawFault != 0) {
        // In Comparator Mode (CR0 bit 2 = 0), the MAX31856 automatically clears the fault status bits
        // in register 0x0F once the fault condition resolves.
        // We intentionally do NOT call clearFaultRegister() here: on 0xFF bus glitches, read-modify-write
        // would overwrite CR0 with 0xFF and corrupt hardware configuration.
        _faultStreak++;

        // SAFETY REVIEW NOTE — Dual-Layer Fault Reporting:
        // Fault flags (reading.fault.*) are decoded from rawFault and ALWAYS reported immediately
        // on every read, regardless of the streak counter. The streak only controls isValid.
        //
        // While _faultStreak < faultStreakLimit:
        //   → reading.isValid  = true   (data passes to PID, no shutdown triggered via isValid)
        //   → reading.fault.*  = set    (SafetyWatchdog can still react via fault.tcHigh etc.)
        //
        // Only once _faultStreak >= faultStreakLimit:
        //   → reading.isValid  = false  (triggers hard SSR shutdown via isValid path)
        //
        // Design intent: transient EMI spikes (1–2 cycles) are ignored; persistent hardware
        // faults are confirmed after (faultStreakLimit × readCycleMs) ms.
        //
        // Consequence: The SafetyWatchdog MUST check reading.fault.tcHigh and reading.fault.openCircuit
        // directly — not rely solely on reading.isValid — for immediate hardware fault reaction.
        if (_faultStreak >= _config.faultStreakLimit) {
            localReading.isValid = false;
            ESP_LOGW(TAG, "MAX31856 CS pin %d fault verified (streak %d/%d): 0x%02X",
                     static_cast<int>(_csPin), _faultStreak, _config.faultStreakLimit, rawFault);
            portENTER_CRITICAL(&_mux);
            _latestReading = localReading;
            portEXIT_CRITICAL(&_mux);
            return localReading;
        }
    } else {
        _faultStreak = 0;
    }

    // Thermocouple Temperature (19-bit signed value, bits 23..5 of the 3-byte register)
    // LTCBL bits[4:0] are reserved by the IC. Mask them defensively before assembly.
    // The >> 5 shift already discards them numerically, but masking avoids relying on that.
    int32_t rawTc = (static_cast<int32_t>(ltcHigh) << 16) |
                    (static_cast<int32_t>(ltcMid)  << 8)  |
                     static_cast<int32_t>(ltcLow & 0xE0); // mask reserved bits[4:0]
    if (rawTc & 0x800000) {
        rawTc |= 0xFF000000; // Sign extend 24-bit to 32-bit signed int
    }
    float rawTemp = static_cast<float>(rawTc >> 5) * 0.0078125f; // LSB = 0.0078125°C (1/128°C)

    // Cold Junction Temperature (14-bit signed value, bits 15..2 of the 2-byte register)
    // CJTL bits[1:0] are reserved; the >> 2 arithmetic shift discards them correctly.
    int16_t rawCj = (static_cast<int16_t>(cjHigh) << 8) | static_cast<int16_t>(cjLow);
    float coldJunctionTemp = static_cast<float>(rawCj >> 2) * 0.015625f; // LSB = 0.015625°C (1/64°C)

    // Check sanity limits (reject obvious open/short SPI noise or all-0xFF disconnected bus)
    if (std::isnan(rawTemp) || rawTemp < -100.0f || rawTemp > 1850.0f) {
        localReading.isValid = false;
        portENTER_CRITICAL(&_mux);
        _latestReading = localReading;
        portEXIT_CRITICAL(&_mux);
        return localReading;
    }

    localReading.rawTemperature = rawTemp;
    localReading.coldJunction   = coldJunctionTemp;
    localReading.isValid        = true;

    // Apply EMA filter
    if (_config.emaFilterEnabled) {
        // Guard: seed the filter ONLY from a clean (fault-free) read.
        // If the first read has a fault (rawFault != 0), rawTemp may be 0°C (open circuit
        // IC default), which would corrupt the filter baseline and cause a slow convergence
        // artifact after startup. Leave _filterInit=false until a clean read arrives.
        if (!_filterInit) {
            if (rawFault == 0) {
                _filteredTemp = rawTemp;
                _filterInit   = true;
            }
            // No clean read yet: pass rawTemp through unfiltered as a safe fallback
            localReading.temperature = rawTemp;
        } else {
            _filteredTemp = (_config.emaAlpha * rawTemp) + ((1.0f - _config.emaAlpha) * _filteredTemp);
            localReading.temperature = _filteredTemp;
        }
    } else {
        localReading.temperature = rawTemp;
        _filteredTemp            = rawTemp;
    }

    portENTER_CRITICAL(&_mux);
    _latestReading = localReading;
    portEXIT_CRITICAL(&_mux);

    return localReading;
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
    portENTER_CRITICAL(&_mux);
    _filteredTemp                 = initialTemp;
    _filterInit                   = true;
    _latestReading.temperature    = initialTemp;
    _latestReading.rawTemperature = initialTemp;
    portEXIT_CRITICAL(&_mux);
}

bool MAX31856::triggerOneShot() {
    uint8_t cr0 = 0x10; // Open circuit detection enabled
    if (_config.filter == NoiseFilter::FILTER_50HZ) cr0 |= (1 << 0);
    cr0 |= (1 << 6); // Set 1SHOT bit
    return writeRegister(REG_CR0, cr0);
}

void MAX31856::clearFaultRegister() {
    // Reconstruct clean CR0 from verified state in RAM (never Read-Modify-Write from bus!).
    // A 0xFF bus glitch could set spurious bits on a bus read, corrupting CR0.
    uint8_t cr0 = 0x10; // Enable Open Circuit fault check (OCFAULT = 01)
    if (_config.mode == ConversionMode::CONTINUOUS) {
        cr0 |= (1 << 7); // CMODE = 1
    }
    if (_config.filter == NoiseFilter::FILTER_50HZ) {
        cr0 |= (1 << 0); // 50Hz rejection
    }
    cr0 |= (1 << 1); // Set FAULTCLR bit
    // Note: FAULTCLR is only effective in Interrupt Fault Mode (CR0 bit 2 = 1).
    // In the default Comparator Mode (CR0 bit 2 = 0), this bit is a no-op —
    // faults clear automatically once the fault condition resolves.
    writeRegister(REG_CR0, cr0);
}

uint32_t MAX31856::conversionTimeMs() const {
    // Based on MAX31856 datasheet Table 2 — First Conversion and 1-Shot Conversion Times.
    // (Note: subsequent auto-mode conversions are faster; this function returns the FIRST/1-SHOT time.)
    //
    // Formula: base_ms + (samples - 1) × per_sample_ms
    // 50Hz filter: base = 169 ms, per additional sample = 40 ms
    // 60Hz filter: base = 143 ms, per additional sample = 34 ms
    //
    // Known values (50Hz):
    //   SAMPLES_1:  169 ms
    //   SAMPLES_2:  209 ms
    //   SAMPLES_4:  289 ms
    //   SAMPLES_8:  449 ms
    //   SAMPLES_16: 769 ms
    const uint32_t baseMs  = (_config.filter == NoiseFilter::FILTER_50HZ) ? 169u : 143u;
    const uint32_t addMs   = (_config.filter == NoiseFilter::FILTER_50HZ) ?  40u :  34u;
    const uint32_t samples = 1u << static_cast<uint32_t>(_config.averaging); // 1,2,4,8,16
    return baseMs + (samples - 1u) * addMs;
}


bool MAX31856::verifyConfig(uint8_t expectedCr0, uint8_t expectedCr1, int8_t expectedCjto) {
    if (_spiHandle == nullptr) return false;

    // Read back registers 0x00 (CR0) through 0x09 (CJTO)
    uint8_t readbackBuf[10] = {0};
    if (!readRegisters(REG_CR0, readbackBuf, sizeof(readbackBuf))) {
        ESP_LOGE(TAG, "MAX31856 CS %d: Failed to read back configuration registers!", static_cast<int>(_csPin));
        return false;
    }

    uint8_t rCr0  = readbackBuf[0]; // 0x00: CR0
    uint8_t rCr1  = readbackBuf[1]; // 0x01: CR1
    uint8_t rMask = readbackBuf[2]; // 0x02: MASK
    int8_t  rCjto = static_cast<int8_t>(readbackBuf[9]); // 0x09: CJTO

    if (rCr0 != expectedCr0 || rCr1 != expectedCr1 || rMask != 0x00 || rCjto != expectedCjto) {
        ESP_LOGE(TAG, "MAX31856 CS %d VERIFICATION FAILED! Read: CR0=0x%02X CR1=0x%02X MASK=0x%02X CJTO=0x%02X "
                      "(Expected: CR0=0x%02X CR1=0x%02X MASK=0x00 CJTO=0x%02X)",
                 static_cast<int>(_csPin), rCr0, rCr1, rMask, static_cast<uint8_t>(rCjto),
                 expectedCr0, expectedCr1, static_cast<uint8_t>(expectedCjto));
        return false;
    }

    ESP_LOGI(TAG, "MAX31856 CS %d verified: CR0=0x%02X, CR1=0x%02X, CJTO=0x%02X (Hardware OK)",
             static_cast<int>(_csPin), rCr0, rCr1, static_cast<uint8_t>(rCjto));
    return true;
}

uint8_t MAX31856::readRegister(uint8_t regAddr) {
    if (_spiHandle == nullptr) return 0;
    // Use polling_transmit for deterministic latency on short transfers.
    // Not thread-safe: must only be called from the dedicated sensor task.
    alignas(4) uint8_t tx[2] = { static_cast<uint8_t>(regAddr & SPI_READ_MASK), 0x00 };
    alignas(4) uint8_t rx[2] = { 0, 0 };
    spi_transaction_t t = {};
    t.length    = 16; // 2 bytes = 16 bits
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_polling_transmit(_spiHandle, &t);
    return rx[1];
}

// ============================================================================
// Low-Level SPI Register Transactions
// ============================================================================

bool MAX31856::readRegisters(uint8_t startReg, uint8_t* buffer, size_t length) {
    if (_spiHandle == nullptr || buffer == nullptr || length == 0) return false;
    // Validate: startReg must be within the MAX31856 register map (0x00–0x0F),
    // and the burst must not extend past the last register.
    if (startReg > 0x0F || (static_cast<uint32_t>(startReg) + length) > 16u) return false;

    // alignas(4): ESP-IDF SPI DMA requires 4-byte aligned buffers.
    // Using stack buffers is safe here because polling_transmit uses hardware FIFO
    // for transfers < 64 bytes (no DMA involved). Alignment is added defensively.
    alignas(4) uint8_t tx[17] = {};
    alignas(4) uint8_t rx[17] = {};
    tx[0] = static_cast<uint8_t>(startReg & SPI_READ_MASK);

    spi_transaction_t t = {};
    t.length    = static_cast<size_t>((length + 1) * 8); // in BITS
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    esp_err_t ret = spi_device_polling_transmit(_spiHandle, &t);
    if (ret != ESP_OK) return false;

    std::memcpy(buffer, &rx[1], length);
    return true;
}

bool MAX31856::writeRegister(uint8_t regAddr, uint8_t value) {
    if (_spiHandle == nullptr) return false;
    alignas(4) uint8_t tx[2] = { static_cast<uint8_t>(regAddr | SPI_WRITE_BIT), value };
    spi_transaction_t t = {};
    t.length    = 16; // 2 bytes = 16 bits
    t.tx_buffer = tx;
    esp_err_t ret = spi_device_polling_transmit(_spiHandle, &t);
    return (ret == ESP_OK);
}

} // namespace sensor
