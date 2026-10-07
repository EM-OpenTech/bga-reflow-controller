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
 * @file max31856.hpp
 * @brief Hardware driver for the MAX31856 precision thermocouple IC.
 *
 * Provides SPI communication, register level configuration, cold junction
 * compensation, open-circuit/overvoltage fault decoding, and EMA filtering.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include <cstdint>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace sensor {

// ============================================================================
// GLOBAL CONFIGURATION CONSTANTS
// Default settings for MAX31856 thermocouple ICs
// ============================================================================

/// Default SPI Clock Speed in Hz (200 kHz for maximum bus stability & long wiring)
constexpr uint32_t DEFAULT_MAX31856_SPI_SPEED_HZ = 200000;

/// Default Exponential Moving Average (EMA) alpha filter factor [0.0 = max smooth, 1.0 = no filter]
constexpr float DEFAULT_EMA_ALPHA = 0.3f;

/// Number of consecutive fault readings required before latching a hardware sensor error
constexpr uint8_t DEFAULT_FAULT_STREAK_THRESHOLD = 3;

/// Default high temperature fault threshold (°C)
constexpr float DEFAULT_TC_HIGH_FAULT_TEMP = 300.0f;

/// Default low temperature fault threshold (°C)
constexpr float DEFAULT_TC_LOW_FAULT_TEMP = -10.0f;

/// Default cold junction high fault threshold (°C)
constexpr float DEFAULT_CJ_HIGH_FAULT_TEMP = 85.0f;

/// Default cold junction low fault threshold (°C)
constexpr float DEFAULT_CJ_LOW_FAULT_TEMP = -20.0f;

// ============================================================================
// MAX31856 ENUMS & DATA STRUCTURES
// ============================================================================

/**
 * @brief Supported thermocouple sensor types according to MAX31856 CR1 register.
 */
enum class ThermocoupleType : uint8_t {
    TYPE_B = 0x00,
    TYPE_E = 0x01,
    TYPE_J = 0x02,
    TYPE_K = 0x03,  ///< Standard for Reflow Stations
    TYPE_N = 0x04,
    TYPE_R = 0x05,
    TYPE_S = 0x06,
    TYPE_T = 0x07
};

/**
 * @brief Mains frequency rejection filter settings.
 */
enum class NoiseFilter : uint8_t {
    FILTER_60HZ = 0x00, ///< 60 Hz mains rejection
    FILTER_50HZ = 0x01  ///< 50 Hz mains rejection (Standard for EU)
};

/**
 * @brief Internal IC sample averaging modes.
 */
enum class AveragingMode : uint8_t {
    SAMPLES_1  = 0x00, ///< 1 sample (no internal averaging)
    SAMPLES_2  = 0x01, ///< 2 samples
    SAMPLES_4  = 0x02, ///< 4 samples (Recommended balance)
    SAMPLES_8  = 0x03, ///< 8 samples
    SAMPLES_16 = 0x04  ///< 16 samples (Max filtering, slower conversion)
};

/**
 * @brief Conversion operating mode.
 */
enum class ConversionMode : uint8_t {
    ONE_SHOT   = 0x00, ///< Manual trigger
    CONTINUOUS = 0x01  ///< Automatic background conversion
};

/// Bitfield representation of MAX31856 Status Register (0x0F)
struct FaultFlags {
    bool    openCircuit        = false; ///< Thermocouple wire disconnected / open
    bool    overUnderVoltage   = false; ///< Input voltage outside allowed range
    bool    tcLow              = false; ///< Thermocouple temp below LTLF threshold
    bool    tcHigh             = false; ///< Thermocouple temp above LTHF threshold
    bool    cjLow              = false; ///< Cold junction temp below CJLF threshold
    bool    cjHigh             = false; ///< Cold junction temp above CJHF threshold
    bool    tcOutOfRange       = false; ///< Thermocouple temp outside physical sensor limits
    bool    cjOutOfRange       = false; ///< Cold junction temp outside IC limits (-55°C to +125°C)
    uint8_t rawByte            = 0x00;  ///< Raw fault register byte

    /**
     * @brief Check if any hardware fault condition is active.
     * @return true if raw register byte is non-zero or any fault flag is set.
     */
    bool hasFault() const {
        return rawByte != 0x00 || openCircuit || overUnderVoltage ||
               tcLow || tcHigh || cjLow || cjHigh || tcOutOfRange || cjOutOfRange;
    }
};

/// Structure holding the output of a single sensor read cycle
struct SensorReading {
    float      temperature    = 0.0f;  ///< Filtered thermocouple temperature in °C
    float      rawTemperature = 0.0f;  ///< Unfiltered raw thermocouple temperature in °C
    float      coldJunction   = 0.0f;  ///< Cold junction internal IC temperature in °C
    FaultFlags fault;                  ///< Decoded fault flags
    uint32_t   timestampMs    = 0;     ///< Timestamp of reading (esp_timer_get_time() in ms)
    bool       isValid        = false; ///< True if reading is valid and verified
};

/// Comprehensive MAX31856 Configuration Structure
struct MAX31856Config {
    ThermocoupleType tcType   = ThermocoupleType::TYPE_K;  ///< Thermocouple chemistry type
    NoiseFilter      filter   = NoiseFilter::FILTER_50HZ;  ///< Mains noise rejection filter (50Hz / 60Hz)
    AveragingMode    averaging = AveragingMode::SAMPLES_4; ///< Internal hardware averaging mode
    ConversionMode   mode     = ConversionMode::CONTINUOUS;///< Continuous or one-shot conversion mode

    float tcHighFaultTemp     = DEFAULT_TC_HIGH_FAULT_TEMP; ///< High thermocouple fault threshold (°C)
    float tcLowFaultTemp      = DEFAULT_TC_LOW_FAULT_TEMP;  ///< Low thermocouple fault threshold (°C)
    float cjHighFaultTemp     = DEFAULT_CJ_HIGH_FAULT_TEMP; ///< High cold-junction fault threshold (°C)
    float cjLowFaultTemp      = DEFAULT_CJ_LOW_FAULT_TEMP;  ///< Low cold-junction fault threshold (°C)
    float cjOffset            = 0.0f;                       ///< Cold junction calibration offset (-8.0°C to +7.9375°C)

    bool    emaFilterEnabled  = true;                          ///< Enable software EMA smoothing filter
    float   emaAlpha          = DEFAULT_EMA_ALPHA;             ///< EMA alpha smoothing factor [0.0..1.0]
    uint8_t faultStreakLimit  = DEFAULT_FAULT_STREAK_THRESHOLD;///< Consecutive fault reads before declaring error
};

/**
 * @class MAX31856
 * @brief Precision hardware driver for MAX31856 thermocouple converter IC.
 *
 * Uses official Espressif driver/spi_master.h for hardware SPI communication.
 * Provides register control, multi-sample fault validation, and EMA filtering.
 *
 * @note **Thread Safety:** read() and all SPI-touching methods MUST be called
 *       exclusively from a single FreeRTOS task. spi_device_polling_transmit()
 *       is NOT thread-safe on a shared device handle. The only method safe to
 *       call from a different task is getLatest(), which is protected by a
 *       spinlock. resetFilter() must not be called concurrently with read().
 */
class MAX31856 {
public:
    /**
     * @brief Construct a MAX31856 driver instance.
     * @param spiHost ESP-IDF SPI host device (use SPI2_HOST or SPI3_HOST;
     *                SPI0_HOST and SPI1_HOST are reserved for Flash/PSRAM)
     * @param csPin Chip Select GPIO pin
     * @param config Configuration parameters
     *
     * @warning **ESP32-S3 N16R8 (8 MB Octal PSRAM):** GPIOs 35–42 are permanently
     *          reserved for the internal Octal PSRAM bus (OSPI). Using any of these
     *          pins for SPI CS, MOSI, MISO, SCLK, or any other peripheral will cause
     *          system crashes or data corruption. Always verify your pin assignment
     *          against the ESP32-S3 datasheet Appendix A before use.
     */
    MAX31856(spi_host_device_t spiHost, gpio_num_t csPin, const MAX31856Config& config = MAX31856Config());

    /**
     * @brief Destroy the MAX31856 instance and remove device from SPI bus.
     */
    ~MAX31856();

    /**
     * @brief Initialize SPI device and write hardware registers.
     *
     * When `skipPriming` is false (default), begin() blocks the calling task for
     * conversionTimeMs() + 50 ms to wait for the first IC conversion to complete,
     * then calls read() once to warm up the EMA filter and populate getLatest().
     *
     * **Multi-sensor parallel initialization pattern** (avoids N × 339 ms serial delay):
     * @code
     * // 1. Init all sensors hardware without blocking:
     * topSensor.begin(true);
     * botSensor.begin(true);
     * // 2. Wait once for the longest conversion time among all sensors:
     * vTaskDelay(pdMS_TO_TICKS(topSensor.conversionTimeMs() + 50));
     * // 3. Prime each sensor's EMA filter:
     * topSensor.read();
     * botSensor.read();
     * @endcode
     *
     * @param skipPriming If true, skips the startup delay and the priming read.
     *                    The caller is responsible for waiting conversionTimeMs()
     *                    before the first read() call.
     * @return true on success, false if SPI device initialization failed.
     */
    bool begin(bool skipPriming = false);

    /**
     * @brief Perform a sensor read cycle (Reads temperature, cold junction, and fault register).
     * @return SensorReading Structure with updated readings and fault flags.
     */
    SensorReading read();

    /**
     * @brief Get last cached sensor reading in a thread-safe manner.
     * @return SensorReading copy of the latest acquired reading.
     */
    SensorReading getLatest() const;

    /**
     * @brief Initialize all hardware registers and clear fault flags (Boot time only).
     * @return true on success, false on communication error.
     */
    bool initHardware();

    /**
     * @brief Apply runtime configuration (software filter settings & selective hardware offsets).
     * @param config New MAX31856Config parameters to apply.
     * @return true on success, false on communication error.
     */
    bool applyConfig(const MAX31856Config& config);

    /**
     * @brief Reset EMA temperature filter to a given initial value.
     * @param initialTemp Initial temperature in °C to seed the filter.
     */
    void resetFilter(float initialTemp);

    /**
     * @brief Trigger a manual one-shot temperature conversion (when in ONE_SHOT mode).
     *
     * @note After calling this method the caller MUST wait at least conversionTimeMs()
     *       milliseconds before calling read(), otherwise the previous (stale) conversion
     *       result is returned without any error indication.
     *       Example: vTaskDelay(pdMS_TO_TICKS(sensor.conversionTimeMs() + 50));
     *
     * @return true on success, false on communication error.
     */
    bool triggerOneShot();

    /**
     * @brief Clear MAX31856 internal hardware fault register.
     *
     * @note This is only effective when the IC is in Interrupt Fault Mode
     *       (CR0 bit 2 = 1). In the default Comparator Mode the FAULTCLR bit
     *       is a no-op — faults clear automatically once the condition resolves.
     */
    void clearFaultRegister();

    /**
     * @brief Calculate the required conversion time for the current config.
     *
     * Returns the time (in ms) for the FIRST or ONE-SHOT conversion to complete.
     * This is longer than subsequent automatic-mode conversions.
     * Derived from MAX31856 datasheet Table 2 (Conversion Times).
     *
     * Use this value as the minimum delay after begin() or triggerOneShot()
     * before calling read() to ensure fresh data is available.
     *
     * @return Conversion time in milliseconds (without safety margin).
     */
    uint32_t conversionTimeMs() const;

    /**
     * @brief Read a single 8-bit register from the MAX31856 IC.
     * @param regAddr Register address (0x00 - 0x0F).
     * @return 8-bit register value.
     */
    uint8_t readRegister(uint8_t regAddr);

    /**
     * @brief Read multiple consecutive registers in a single atomic SPI transaction.
     * @param startReg Starting register address (0x00 - 0x0F).
     * @param buffer Output buffer to receive register bytes.
     * @param length Number of bytes to read.
     * @return true on success, false on error.
     */
    bool readRegisters(uint8_t startReg, uint8_t* buffer, size_t length);

    /**
     * @brief Write a single 8-bit value to a MAX31856 register.
     * @param regAddr Register address (0x00 - 0x0F).
     * @param value 8-bit value to write.
     * @return true on success, false on error.
     */
    bool writeRegister(uint8_t regAddr, uint8_t value);

    /**
     * @brief Verify written configuration registers against expected values via readback.
     * @param expectedCr0 Expected CR0 register value
     * @param expectedCr1 Expected CR1 register value
     * @param expectedCjto Expected CJTO register value
     * @return true if all registers match expected values, false otherwise.
     */
    bool verifyConfig(uint8_t expectedCr0, uint8_t expectedCr1, int8_t expectedCjto);

private:
    spi_host_device_t   _spiHost;                 ///< ESP-IDF SPI host identifier
    gpio_num_t          _csPin;                   ///< Dedicated Chip Select GPIO pin
    spi_device_handle_t _spiHandle = nullptr;     ///< ESP-IDF SPI device handle
    MAX31856Config      _config;                  ///< Active sensor configuration

    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED; ///< Critical section for thread-safe readings
    SensorReading _latestReading;                 ///< Latest acquired reading cache
    float         _filteredTemp = 25.0f;          ///< Running exponential moving average temperature (°C)
    bool          _filterInit   = false;          ///< Initialization flag for EMA filter seed
    uint8_t       _faultStreak  = 0;              ///< Consecutive fault counter for noise suppression

    void parseFaultRegister(uint8_t rawFault, FaultFlags& flags);
    void writeThresholds();
    void writeCjtoOffset(float offset);
};

} // namespace sensor

