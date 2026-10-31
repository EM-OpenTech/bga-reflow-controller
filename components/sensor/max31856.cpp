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
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {

constexpr const char* TAG = "MAX31856";

// MAX31856 Register Map Addresses
constexpr uint8_t REG_CR0 = 0x00;
constexpr uint8_t REG_CR1 = 0x01;
constexpr uint8_t REG_MASK = 0x02;
constexpr uint8_t REG_CJHF = 0x03;
constexpr uint8_t REG_CJLF = 0x04;
constexpr uint8_t REG_LTHFTH = 0x05;
constexpr uint8_t REG_LTHFTL = 0x06;
constexpr uint8_t REG_LTLFTH = 0x07;
constexpr uint8_t REG_LTLFTL = 0x08;
constexpr uint8_t REG_CJTO = 0x09;
constexpr uint8_t REG_CJTH = 0x0A;
constexpr uint8_t REG_CJTL = 0x0B;
constexpr uint8_t REG_LTCBH = 0x0C;
constexpr uint8_t REG_LTCBM = 0x0D;
constexpr uint8_t REG_LTCBL = 0x0E;
constexpr uint8_t REG_SR = 0x0F;

constexpr uint8_t SPI_WRITE_BIT = 0x80;
constexpr uint8_t SPI_READ_MASK = 0x7F;

} // anonymous namespace

namespace sensor {

// ============================================================================
// Lifecycle & SPI Hardware Initialization
// ============================================================================

MAX31856::MAX31856(spi_host_device_t spiHost, gpio_num_t csPin, const MAX31856Config& config)
    : _spiHost(spiHost), _csPin(csPin), _config(config) {}

MAX31856::~MAX31856() {
    if (_spiHandle != nullptr) {
        spi_bus_remove_device(_spiHandle);
        _spiHandle = nullptr;
    }
}

bool MAX31856::begin(bool skipPriming) {
    // Guard against double initialization.
    // spi_bus_add_device() must not be called twice on the same instance — it would
    // either assert or leak the previous handle. If already initialized, return true.
    if (_spiHandle != nullptr) {
        ESP_LOGW(TAG, "CS pin %d: begin() called on already-initialized device — ignored.", static_cast<int>(_csPin));
        return true;
    }

    spi_device_interface_config_t devCfg = {};
    devCfg.mode = 1;                                       // MAX31856 SPI Mode 1 (CPOL=0, CPHA=1)
    devCfg.clock_speed_hz = DEFAULT_MAX31856_SPI_SPEED_HZ; // 200 kHz (maximum EMI noise immunity)
    devCfg.spics_io_num = _csPin;
    devCfg.queue_size = 1;
    // Hardware CS setup & hold times:
    // On ESP32-S3 GPSPI, cs_ena_pretrans & cs_ena_posttrans control hardware CS leading/trailing cycles.
    // At 200 kHz (1 cycle = 5 µs), 2 cycles provide 10 µs setup (tCSS) and 10 µs hold (tCSH).
    // Datasheet min is 100 ns. This 10 µs buffer guarantees clean CS level latching across all PCB variants
    // (including unbuffered bare breakout boards with parasitic diode/cable capacitance).
    devCfg.cs_ena_pretrans = 2;  // 2 cycles = 10 µs CS leading delay before SCLK starts
    devCfg.cs_ena_posttrans = 2; // 2 cycles = 10 µs CS trailing delay after SCLK ends
    devCfg.input_delay_ns = 0;   // No artificial phase offset required at 200 kHz

    esp_err_t ret = spi_bus_add_device(_spiHost, &devCfg, &_spiHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device on CS pin %d: %s", static_cast<int>(_csPin), esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "SPI device added on CS pin %d (Speed: %u Hz, CS pre/post: 2 cycles / 10us)",
             static_cast<int>(_csPin), static_cast<unsigned>(DEFAULT_MAX31856_SPI_SPEED_HZ));

    // Retry hardware initialization up to 3 times with a 50 ms pause between attempts.
    // Rationale: on boards with marginal power supply decoupling, the MAX31856 may experience
    // a brief brownout immediately after the ESP32 SPI bus is activated, causing the first
    // initHardware() attempt to fail (CR0 write succeeds, subsequent writes miss → MASK=0xFF).
    // A short delay and retry gives the IC time to stabilise its internal supply rails.
    constexpr int kMaxInitRetries = 3;
    bool initOk = false;
    _porFaultLogged = false;
    _faultLogged = false;
    _faultStreak = 0;
    for (int attempt = 1; attempt <= kMaxInitRetries; ++attempt) {
        initOk = initHardware();
        if (initOk) {
            break;
        }
        ESP_LOGW(TAG, "CS %d: initHardware() failed (attempt %d/%d) — retrying in 50 ms...", static_cast<int>(_csPin),
                 attempt, kMaxInitRetries);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!initOk) {
        ESP_LOGE(TAG, "CS %d: initHardware() failed after %d attempts — sensor unavailable!", static_cast<int>(_csPin),
                 kMaxInitRetries);
        spi_bus_remove_device(_spiHandle);
        _spiHandle = nullptr;
        return false;
    }

    if (skipPriming) {
        // Caller is responsible for waiting conversionTimeMs() before the first read().
        // Use this for parallel multi-sensor init to avoid N × blockingDelay overhead.
        // See begin() header docstring for the recommended pattern.
        ESP_LOGI(TAG, "CS pin %d: priming skipped — caller must wait >= %u ms before read()", static_cast<int>(_csPin),
                 conversionTimeMs());
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
    if (_config.mode != ConversionMode::CONTINUOUS) {
        return true;
    }

    const uint32_t primeMs = conversionTimeMs() + 50u;
    ESP_LOGI(TAG, "Waiting %u ms for first conversion (SAMPLES=%d, %sHz filter)...", primeMs,
             1 << static_cast<int>(_config.averaging), _config.filter == NoiseFilter::FILTER_50HZ ? "50" : "60");
    vTaskDelay(pdMS_TO_TICKS(primeMs));
    read();

    return true;
}

bool MAX31856::initHardware(bool verbose) {
    if (_spiHandle == nullptr) {
        return false;
    }

    // ========================================================================
    // Power-On Self-Test (POST): Hardware Bus & Register Toggle Verification
    // ========================================================================
    // Actively verifies that bidirectional SPI communication works reliably on this CS pin.
    // Writes alternating values to CR1 (0x03 = 1x avg / Type-K, then 0x23 = 4x avg / Type-K)
    // and reads each back immediately. This proves:
    //  1. CS line transitions cleanly and satisfies tCSS setup time.
    //  2. MOSI line shifts bits into the IC without missing the MSB write-command bit.
    //  3. SCLK clocking is synchronized and MISO level latching is verified.
    // If the IC is absent, stuck, or bus wiring is flawed (e.g. read back 0x00 or 0xFF),
    // the POST fails immediately with a descriptive diagnostic log.
    constexpr std::array<uint8_t, 2> POST_PATTERNS = {0x03, 0x23}; // Pattern 1: 1x, Pattern 2: 4x (Type-K)

    for (const uint8_t pattern : POST_PATTERNS) {
        if (!writeRegister(REG_CR1, pattern)) {
            if (verbose) {
                ESP_LOGE(TAG, "CS %d POST FAILED: SPI write error while sending pattern 0x%02X",
                         static_cast<int>(_csPin), pattern);
            }
            return false;
        }
        const uint8_t readVal = readRegister(REG_CR1);
        if (readVal != pattern) {
            if (verbose) {
                ESP_LOGE(TAG, "CS %d POST FAILED: Wrote CR1=0x%02X, read back 0x%02X! (Hardware bus/wiring error)",
                         static_cast<int>(_csPin), pattern, readVal);
            }
            return false;
        }
    }

    if (verbose) {
        ESP_LOGI(TAG, "CS %d POST PASSED: Hardware SPI register toggle verified (0x%02X <-> 0x%02X OK)",
                 static_cast<int>(_csPin), POST_PATTERNS[0], POST_PATTERNS[1]);
    }

    // Step 1: Place MAX31856 into 'Normally Off' mode (CMODE=0) during configuration.
    // Datasheet Note (p. 19) & Linux kernel driver: Changing TC type, averaging, or filter notch
    // frequencies must occur while CMODE=0 to avoid internal DSP / filter state corruption.
    if (!writeRegister(REG_CR0, Cr0Bits::OCFAULT_0)) { // CMODE=0, Open Circuit check enabled
        if (verbose) {
            ESP_LOGE(TAG, "CS %d: SPI write failed at Step 1 (CR0=0x%02X)", static_cast<int>(_csPin),
                     Cr0Bits::OCFAULT_0);
        }
        return false;
    }

    // Step 2: Configure CR1: Averaging Mode (bits 6:4) + Thermocouple Type (bits 3:0)
    const uint8_t cr1 = detail::computeCr1(_config.averaging, _config.tcType);
    if (!writeRegister(REG_CR1, cr1)) {
        if (verbose) {
            ESP_LOGE(TAG, "CS %d: SPI write failed at Step 2 (CR1=0x%02X)", static_cast<int>(_csPin), cr1);
        }
        return false;
    }

    // Step 3: Configure Fault Mask Register (0x02 / MASK):
    // Datasheet power-on default is 0xFF (all fault bits masked → FAULT pin never asserts).
    // Writing 0x00 unmasks all fault conditions, enabling the hardware FAULT pin for any fault.
    // This allows external hardware fault detection independent of software polling.
    // Even with mask=0xFF, all faults are still readable in the SR register (0x0F).
    if (!writeRegister(REG_MASK, 0x00)) {
        if (verbose) {
            ESP_LOGE(TAG, "CS %d: SPI write failed at Step 3 (MASK=0x00)", static_cast<int>(_csPin));
        }
        return false;
    }

    // Step 4: Write High/Low temperature threshold registers and CJTO
    if (!writeThresholds()) {
        if (verbose) {
            ESP_LOGE(TAG, "CS %d: SPI write failed at Step 4 (thresholds/CJTO)", static_cast<int>(_csPin));
        }
        return false;
    }

    // Step 5: Activate target conversion mode and 50Hz/60Hz filter in CR0
    const uint8_t cr0 = detail::computeCr0(_config.mode, _config.filter);
    if (!writeRegister(REG_CR0, cr0)) {
        if (verbose) {
            ESP_LOGE(TAG, "CS %d: SPI write failed at Step 5 (CR0=0x%02X)", static_cast<int>(_csPin), cr0);
        }
        return false;
    }

    // Step 6: Verify configuration via readback comparison
    const int8_t expectedCjto = detail::encodeCjto(_config.cjOffset);
    if (!verifyConfig(cr0, cr1, expectedCjto)) {
        return false;
    }

    if (verbose) {
        constexpr std::array<const char*, 8> TC_NAMES = {"B", "E", "J", "K", "N", "R", "S", "T"};
        const auto tcIdx = static_cast<uint8_t>(_config.tcType);
        ESP_LOGI(TAG, "MAX31856 hardware initialized: CS=%d, Type=%s, Mode=%s, Filter=%s, Averaging=%dx",
                 static_cast<int>(_csPin), (tcIdx < TC_NAMES.size()) ? TC_NAMES.at(tcIdx) : "?",
                 (_config.mode == ConversionMode::CONTINUOUS) ? "CONTINUOUS" : "ONE_SHOT",
                 (_config.filter == NoiseFilter::FILTER_50HZ) ? "50Hz" : "60Hz",
                 1 << static_cast<int>(_config.averaging));
    }

    return true;
}

bool MAX31856::applyConfig(const MAX31856Config& config) {
    if (_spiHandle == nullptr) {
        _config = config;
        return false;
    }

    // 1. Update Cold Junction Offset only if value numerically changed
    const bool cjtoChanged = (std::fabs(_config.cjOffset - config.cjOffset) > 0.001f);
    if (cjtoChanged) {
        _config.cjOffset = config.cjOffset;
        if (!writeCjtoOffset(_config.cjOffset)) {
            ESP_LOGE(TAG, "MAX31856 CS %d: CJTO write failed!", static_cast<int>(_csPin));
            return false;
        }
    }

    // 2. Update software filter & fault parameters in RAM (no SPI traffic)
    _config.emaFilterEnabled = config.emaFilterEnabled;
    if (std::isfinite(config.emaAlpha) && config.emaAlpha >= 0.0f && config.emaAlpha <= 1.0f) {
        _config.emaAlpha = config.emaAlpha;
    }
    // Reset the fault streak counter whenever the limit threshold changes.
    // Without this, an existing streak accumulated under the old limit could
    // immediately exceed a newly lowered limit and falsely declare a fault.
    if (_config.faultStreakLimit != config.faultStreakLimit) {
        _config.faultStreakLimit = config.faultStreakLimit;
        _faultStreak = 0;
    }

    // 3. Update hardware threshold registers only if changed
    const bool thresholdsChanged =
        (_config.tcHighFaultTemp != config.tcHighFaultTemp || _config.tcLowFaultTemp != config.tcLowFaultTemp ||
         _config.cjHighFaultTemp != config.cjHighFaultTemp || _config.cjLowFaultTemp != config.cjLowFaultTemp);
    if (thresholdsChanged) {
        _config.tcHighFaultTemp = config.tcHighFaultTemp;
        _config.tcLowFaultTemp = config.tcLowFaultTemp;
        _config.cjHighFaultTemp = config.cjHighFaultTemp;
        _config.cjLowFaultTemp = config.cjLowFaultTemp;
        if (!writeThresholds()) {
            ESP_LOGE(TAG, "MAX31856 CS %d: threshold write failed!", static_cast<int>(_csPin));
            return false;
        }
    }

    // 4. Update CR0 / CR1 only if operating mode or TC type changed
    const bool crRegistersChanged = (_config.tcType != config.tcType || _config.filter != config.filter ||
                                     _config.averaging != config.averaging || _config.mode != config.mode);
    if (crRegistersChanged) {
        _config.tcType = config.tcType;
        _config.filter = config.filter;
        _config.averaging = config.averaging;
        _config.mode = config.mode;

        // Temporarily pause conversion (CMODE=0) while updating notch filter and TC type
        writeRegister(REG_CR0, Cr0Bits::OCFAULT_0);
        writeRegister(REG_CR1, detail::computeCr1(_config.averaging, _config.tcType));
        writeRegister(REG_CR0, detail::computeCr0(_config.mode, _config.filter));
    }

    // 5. Verify all modified registers via atomic burst readback
    // CR0/CR1 unchanged but thresholds or CJTO were rewritten — verify via readback.
    // Recompute expected CR0/CR1 from active config (no SPI write, just for verifyConfig comparison).
    if (crRegistersChanged || thresholdsChanged || cjtoChanged) {
        const uint8_t expectedCr0 = detail::computeCr0(_config.mode, _config.filter);
        const uint8_t expectedCr1 = detail::computeCr1(_config.averaging, _config.tcType);
        // Verify all registers after runtime config update (CR0, CR1, MASK, CJTO, all thresholds).
        // 10-byte readback ≈ 440 µs @ 200 kHz — acceptable for an infrequent config change.
        const int8_t expectedCjto = detail::encodeCjto(_config.cjOffset);
        if (!verifyConfig(expectedCr0, expectedCr1, expectedCjto)) {
            ESP_LOGE(TAG, "MAX31856 CS %d: threshold/CJTO update verification failed!", static_cast<int>(_csPin));
            return false;
        }
    }

    ESP_LOGI(TAG, "MAX31856 runtime config updated: CJTO=%.2f°C, EMA_alpha=%.2f, faultStreakLimit=%d",
             static_cast<double>(_config.cjOffset), static_cast<double>(_config.emaAlpha),
             static_cast<int>(_config.faultStreakLimit));
    return true;
}

bool MAX31856::writeCjtoOffset(float offset) {
    // Cold Junction Temperature Offset (CJTO 0x09: signed 8-bit, LSB = 0.0625°C, range -8°C to +7.9375°C)
    return writeRegister(REG_CJTO, static_cast<uint8_t>(detail::encodeCjto(offset)));
}

bool MAX31856::writeThresholds() {
    // Cold Junction High / Low thresholds (REG_CJHF 0x03, REG_CJLF 0x04)
    // Datasheet: signed 8-bit integer, LSB = 1°C, range -128°C to +127°C.
    // Bug fix: The original cast truncated toward zero (e.g., 85.9°C → 85) and
    //   was undefined behavior for float values outside [-128, 127]. Using
    //   std::round() + std::clamp() prevents both issues.
    bool ok = true;
    ok &= writeRegister(REG_CJHF, detail::encodeCjThreshold(_config.cjHighFaultTemp));
    ok &= writeRegister(REG_CJLF, detail::encodeCjThreshold(_config.cjLowFaultTemp));

    // Cold Junction Temperature Offset (CJTO 0x09: signed 8-bit, LSB = 0.0625°C, range -8°C to +7.9375°C)
    ok &= writeCjtoOffset(_config.cjOffset);

    // Thermocouple High threshold (LTHFTH/LTHFTL 0x05/0x06: 16-bit signed, LSB = 0.0625°C)
    // Range: [-2048°C, +2047.9375°C]. Clamp to prevent int16_t overflow UB.
    const int16_t tcHighRaw = detail::encodeTcThreshold(_config.tcHighFaultTemp);
    ok &= writeRegister(REG_LTHFTH, static_cast<uint8_t>((tcHighRaw >> 8) & 0xFF));
    ok &= writeRegister(REG_LTHFTL, static_cast<uint8_t>(tcHighRaw & 0xFF));

    // Thermocouple Low threshold (LTLFTH/LTLFTL 0x07/0x08: 16-bit signed, LSB = 0.0625°C)
    const int16_t tcLowRaw = detail::encodeTcThreshold(_config.tcLowFaultTemp);
    ok &= writeRegister(REG_LTLFTH, static_cast<uint8_t>((tcLowRaw >> 8) & 0xFF));
    ok &= writeRegister(REG_LTLFTL, static_cast<uint8_t>(tcLowRaw & 0xFF));

    return ok;
}

SensorReading MAX31856::read() {
    SensorReading localReading;
    localReading.timestampMs = static_cast<uint32_t>(static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL);

    // Guard 1: SPI device must be allocated
    if (_spiHandle == nullptr) {
        localReading.isValid = false;
        return localReading;
    }

    // Guard 2: Single atomic burst read of ALL registers: 0x00 (CR0) through 0x0F (SR) — 16 bytes.
    // 16 bytes @ 200 kHz SPI takes ~680 µs (negligible in 200ms cycle).
    std::array<uint8_t, 16> burstBuf{};
    if (!readRegisters(REG_CR0, burstBuf.data(), burstBuf.size())) {
        localReading.isValid = false;
        return localReading;
    }

    const uint8_t rCr0 = burstBuf[REG_CR0];
    const uint8_t rMask = burstBuf[REG_MASK];
    const uint8_t rawFault = burstBuf[REG_SR];

    // Guard 3: Power-On Reset (POR) / Brownout auto-recovery
    if (isPorCondition(rCr0, rMask)) {
        handlePorRecovery(rCr0, rMask);
        localReading.isValid = false;
        return localReading;
    }
    notifyPorRestoredIfPending();

    // Guard 4: Parse fault register and process fault streak deglitching
    parseFaultRegister(rawFault, localReading.fault);
    if (!processFaultStreak(rawFault)) {
        localReading.isValid = false;
        return localReading;
    }

    // Guard 5: Decode & validate physical sensor temperature boundaries
    const float rawTemp = detail::decodeThermocoupleTemp(burstBuf[REG_LTCBH], burstBuf[REG_LTCBM], burstBuf[REG_LTCBL]);
    if (!isTemperatureSane(rawTemp)) {
        localReading.isValid = false;
        return localReading;
    }

    // 6. Valid sensor reading: compute cold junction and apply EMA filter
    localReading.rawTemperature = rawTemp;
    localReading.coldJunction = detail::decodeColdJunctionTemp(burstBuf[REG_CJTH], burstBuf[REG_CJTL]);
    localReading.temperature = updateEmaFilter(rawTemp, rawFault);
    localReading.isValid = true;

    return localReading;
}

bool MAX31856::isPorCondition(uint8_t rCr0, uint8_t rMask) const {
    // Detect Power-On Reset / Brownout:
    // If we are in CONTINUOUS mode but CR0 bit 7 (CMODE) is 0, or if MASK reverted to 0xFF (POR default),
    // the MAX31856 lost power and reset.
    //
    // @note ONE_SHOT mode limitation: The CMODE==0 check is skipped in ONE_SHOT mode because
    //       CMODE is always 0 there by design. POR is still partially caught via the MASK==0xFF fallback.
    return (_config.mode == ConversionMode::CONTINUOUS && ((rCr0 & Cr0Bits::CMODE) == 0)) || (rMask == 0xFF);
}

void MAX31856::handlePorRecovery(uint8_t rCr0, uint8_t rMask) {
    _filterInit = false; // Reset EMA filter seed so stale 0°C is not smoothed

    if (!_porFaultLogged) {
        _porFaultLogged = true;
        ESP_LOGE(TAG,
                 "MAX31856 CS %d: IC Power-On Reset / Brownout detected! CR0=0x%02X, MASK=0x%02X. Re-initializing...",
                 static_cast<int>(_csPin), rCr0, rMask);
        if (!initHardware(true)) {
            ESP_LOGE(TAG, "MAX31856 CS %d: POR recovery re-init failed — sensor unavailable until next cycle!",
                     static_cast<int>(_csPin));
        }
        return;
    }

    // Silent retry in background to eliminate terminal log spamming
    if (initHardware(false)) {
        _porFaultLogged = false;
        ESP_LOGI(TAG, "MAX31856 CS %d: Hardware recovered after POR / brownout.", static_cast<int>(_csPin));
    }
}

void MAX31856::notifyPorRestoredIfPending() {
    if (_porFaultLogged) {
        _porFaultLogged = false;
        ESP_LOGI(TAG, "MAX31856 CS %d: Hardware communication restored.", static_cast<int>(_csPin));
    }
}

bool MAX31856::processFaultStreak(uint8_t rawFault) {
    if (rawFault == 0) {
        _faultStreak = 0;
        _faultLogged = false;
        return true;
    }

    // Cap at faultStreakLimit before incrementing to prevent uint8_t overflow.
    if (_faultStreak < _config.faultStreakLimit) {
        _faultStreak++;
    }

    // While _faultStreak < faultStreakLimit, reading remains valid for noise deglitching.
    if (_faultStreak < _config.faultStreakLimit) {
        return true;
    }

    // Fault streak limit reached: verify error and log warning once
    if (!_faultLogged) {
        _faultLogged = true;
        ESP_LOGW(TAG, "MAX31856 CS pin %d fault verified (streak %d/%d): 0x%02X", static_cast<int>(_csPin),
                 _faultStreak, _config.faultStreakLimit, rawFault);
    }
    return false;
}

bool MAX31856::isTemperatureSane(float temp) {
    return std::isfinite(temp) && (temp >= -100.0f) && (temp <= 1850.0f);
}

float MAX31856::updateEmaFilter(float rawTemp, uint8_t rawFault) {
    if (!std::isfinite(rawTemp)) {
        return _filteredTemp;
    }
    if (!_config.emaFilterEnabled) {
        _filteredTemp = rawTemp;
        return rawTemp;
    }

    // Guard: seed the filter ONLY from a clean (fault-free) read.
    // If the first read has a fault (rawFault != 0), rawTemp may be 0°C (open circuit default),
    // which would corrupt the filter baseline and cause a slow convergence artifact after startup.
    if (!_filterInit) {
        if (rawFault == 0) {
            _filteredTemp = rawTemp;
            _filterInit = true;
        }
        return rawTemp;
    }

    _filteredTemp = (_config.emaAlpha * rawTemp) + ((1.0f - _config.emaAlpha) * _filteredTemp);
    return _filteredTemp;
}

void MAX31856::parseFaultRegister(uint8_t rawFault, FaultFlags& flags) {
    flags.rawByte = rawFault;
    flags.cjOutOfRange = (rawFault & SrBits::CJ_RANGE) != 0;
    flags.tcOutOfRange = (rawFault & SrBits::TC_RANGE) != 0;
    flags.cjHigh = (rawFault & SrBits::CJ_HIGH) != 0;
    flags.cjLow = (rawFault & SrBits::CJ_LOW) != 0;
    flags.tcHigh = (rawFault & SrBits::TC_HIGH) != 0;
    flags.tcLow = (rawFault & SrBits::TC_LOW) != 0;
    flags.overUnderVoltage = (rawFault & SrBits::OVER_UNDER_VOLT) != 0;
    flags.openCircuit = (rawFault & SrBits::OPEN_CIRCUIT) != 0;
}

void MAX31856::resetFilter(float initialTemp) {
    if (!std::isfinite(initialTemp)) {
        initialTemp = 25.0f;
    }
    _filteredTemp = initialTemp;
    _filterInit = true;
}

bool MAX31856::triggerOneShot() {
    uint8_t cr0 = Cr0Bits::OCFAULT_0 | Cr0Bits::ONE_SHOT;
    if (_config.filter == NoiseFilter::FILTER_50HZ) {
        cr0 |= Cr0Bits::FILTER_50HZ;
    }
    return writeRegister(REG_CR0, cr0);
}

void MAX31856::clearFaultRegister() {
    // Reconstruct clean CR0 from verified state in RAM (never Read-Modify-Write from bus!).
    // A 0xFF bus glitch could set spurious bits on a bus read, corrupting CR0.
    uint8_t cr0 = detail::computeCr0(_config.mode, _config.filter);
    cr0 |= Cr0Bits::FAULT_CLEAR;
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
    return detail::calculateConversionTimeMs(_config.filter, _config.averaging);
}

bool MAX31856::verifyConfig(uint8_t expectedCr0, uint8_t expectedCr1, int8_t expectedCjto) {
    if (_spiHandle == nullptr) {
        return false;
    }

    // Read back registers 0x00 (CR0) through 0x09 (CJTO) — 10 bytes in one atomic burst.
    // Cost: (10+1) bytes × 8 bits / 200000 Hz ≈ 440 µs. Acceptable for a one-time boot check.
    std::array<uint8_t, 10> readbackBuf{};
    if (!readRegisters(REG_CR0, readbackBuf.data(), readbackBuf.size())) {
        ESP_LOGE(TAG, "MAX31856 CS %d: Failed to read back configuration registers!", static_cast<int>(_csPin));
        return false;
    }

    // Recompute expected threshold register bytes from active config (same logic as writeThresholds())

    const uint8_t rCr0 = readbackBuf[0];                    // 0x00: CR0
    const uint8_t rCr1 = readbackBuf[1];                    // 0x01: CR1
    const uint8_t rMask = readbackBuf[2];                   // 0x02: MASK
    const uint8_t rCjhf = readbackBuf[3];                   // 0x03: CJHF
    const uint8_t rCjlf = readbackBuf[4];                   // 0x04: CJLF
    const uint8_t rLthfth = readbackBuf[5];                 // 0x05: LTHFTH
    const uint8_t rLthftl = readbackBuf[6];                 // 0x06: LTHFTL
    const uint8_t rLtlfth = readbackBuf[7];                 // 0x07: LTLFTH
    const uint8_t rLtlftl = readbackBuf[8];                 // 0x08: LTLFTL
    const auto rCjto = static_cast<int8_t>(readbackBuf[9]); // 0x09: CJTO

    const uint8_t expCjhf = detail::encodeCjThreshold(_config.cjHighFaultTemp);
    const uint8_t expCjlf = detail::encodeCjThreshold(_config.cjLowFaultTemp);
    const int16_t tcHighRaw = detail::encodeTcThreshold(_config.tcHighFaultTemp);
    const int16_t tcLowRaw = detail::encodeTcThreshold(_config.tcLowFaultTemp);

    const auto expLthfth = static_cast<uint8_t>((tcHighRaw >> 8) & 0xFF);
    const auto expLthftl = static_cast<uint8_t>(tcHighRaw & 0xFF);
    const auto expLtlfth = static_cast<uint8_t>((tcLowRaw >> 8) & 0xFF);
    const auto expLtlftl = static_cast<uint8_t>(tcLowRaw & 0xFF);

    // Check control registers
    if (rCr0 != expectedCr0 || rCr1 != expectedCr1 || rMask != 0x00 || rCjto != expectedCjto) {
        ESP_LOGE(TAG,
                 "MAX31856 CS %d CONTROL REGISTERS MISMATCH! "
                 "Read: CR0=0x%02X CR1=0x%02X MASK=0x%02X CJTO=0x%02X "
                 "(Expected: CR0=0x%02X CR1=0x%02X MASK=0x00 CJTO=0x%02X)",
                 static_cast<int>(_csPin), rCr0, rCr1, rMask, static_cast<uint8_t>(rCjto), expectedCr0, expectedCr1,
                 0x00, static_cast<uint8_t>(expectedCjto));
        return false;
    }

    // Check threshold registers (same 10-byte readback, zero additional SPI cost)
    if (rCjhf != expCjhf || rCjlf != expCjlf || rLthfth != expLthfth || rLthftl != expLthftl || rLtlfth != expLtlfth ||
        rLtlftl != expLtlftl) {
        ESP_LOGE(TAG,
                 "MAX31856 CS %d THRESHOLD REGISTERS MISMATCH! "
                 "CJHF=0x%02X(exp 0x%02X) CJLF=0x%02X(exp 0x%02X) "
                 "TC_H=0x%02X%02X(exp 0x%02X%02X) TC_L=0x%02X%02X(exp 0x%02X%02X)",
                 static_cast<int>(_csPin), rCjhf, expCjhf, rCjlf, expCjlf, rLthfth, rLthftl, expLthfth, expLthftl,
                 rLtlfth, rLtlftl, expLthfth, expLtlftl);
        return false;
    }

    ESP_LOGI(TAG,
             "MAX31856 CS %d fully verified: CR0=0x%02X CR1=0x%02X CJTO=0x%02X "
             "CJHF=0x%02X CJLF=0x%02X TC_H=0x%02X%02X TC_L=0x%02X%02X (Hardware OK)",
             static_cast<int>(_csPin), rCr0, rCr1, static_cast<uint8_t>(rCjto), rCjhf, rCjlf, rLthfth, rLthftl, rLtlfth,
             rLtlftl);
    return true;
}

uint8_t MAX31856::readRegister(uint8_t regAddr) {
    if (_spiHandle == nullptr) {
        return 0xFF;
    }
    // Use polling_transmit for deterministic latency on short transfers.
    // Not thread-safe: must only be called from the dedicated sensor task.
    alignas(4) std::array<uint8_t, 2> tx = {static_cast<uint8_t>(regAddr & SPI_READ_MASK), 0x00};
    alignas(4) std::array<uint8_t, 2> rx = {};
    spi_transaction_t t = {};
    t.length = 16;
    t.tx_buffer = tx.data();
    t.rx_buffer = rx.data();
    esp_err_t ret = spi_device_polling_transmit(_spiHandle, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "readRegister(0x%02X) SPI error: %s", regAddr, esp_err_to_name(ret));
        return 0xFF; // Return all-bits-set — distinguishable from 0x00 and fails sanity checks
    }
    return rx.at(1);
}

// ============================================================================
// Low-Level SPI Register Transactions
// ============================================================================

bool MAX31856::readRegisters(uint8_t startReg, uint8_t* buffer, size_t length) {
    if (_spiHandle == nullptr || buffer == nullptr || length == 0) {
        return false;
    }
    // Validate: startReg must be within the MAX31856 register map (0x00–0x0F),
    // and the burst must not extend past the last register.
    if (startReg > 0x0F || (static_cast<uint32_t>(startReg) + length) > 16u) {
        return false;
    }

    // alignas(4): ESP-IDF SPI DMA requires 4-byte aligned buffers.
    // Using stack buffers is safe here because polling_transmit uses hardware FIFO
    // for transfers < 64 bytes (no DMA involved). Alignment is added defensively.
    alignas(4) std::array<uint8_t, 17> tx{};
    alignas(4) std::array<uint8_t, 17> rx{};
    tx.at(0) = static_cast<uint8_t>(startReg & SPI_READ_MASK);

    spi_transaction_t t = {};
    t.length = static_cast<size_t>((length + 1) * 8); // in BITS
    t.tx_buffer = tx.data();
    t.rx_buffer = rx.data();

    const bool ok = (spi_device_polling_transmit(_spiHandle, &t) == ESP_OK);
    if (ok) {
        std::memcpy(buffer, &rx.at(1), length);
    }
    return ok;
}

bool MAX31856::writeRegister(uint8_t regAddr, uint8_t value) {
    if (_spiHandle == nullptr) {
        return false;
    }
    alignas(4) std::array<uint8_t, 2> tx = {static_cast<uint8_t>(regAddr | SPI_WRITE_BIT), value};
    spi_transaction_t t = {};
    t.length = 16;
    t.tx_buffer = tx.data();
    esp_err_t ret = spi_device_polling_transmit(_spiHandle, &t);
    return (ret == ESP_OK);
}

} // namespace sensor
