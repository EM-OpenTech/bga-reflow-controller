/**
 * @file max31856.hpp
 * @brief Native ESP-IDF hardware driver for the MAX31856 thermocouple IC.
 * @author BGA Reflow Controller Team
 */

#pragma once

#include <cstdint>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"

namespace sensor {

// ============================================================================
// GLOBAL CONFIGURATION CONSTANTS (At top of header)
// Easily adjustable default settings for MAX31856 thermocouple ICs
// ============================================================================

/// Default SPI Clock Speed in Hz (2 MHz for MAX31856)
constexpr uint32_t DEFAULT_MAX31856_SPI_SPEED_HZ = 2000000;

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
    TYPE_K = 0x03,  // Standard for Reflow Stations
    TYPE_N = 0x04,
    TYPE_R = 0x05,
    TYPE_S = 0x06,
    TYPE_T = 0x07
};

/**
 * @brief Mains frequency rejection filter settings.
 */
enum class NoiseFilter : uint8_t {
    FILTER_60HZ = 0x00, // 60 Hz mains rejection
    FILTER_50HZ = 0x01  // 50 Hz mains rejection (Standard for EU)
};

/**
 * @brief Internal IC sample averaging modes.
 */
enum class AveragingMode : uint8_t {
    SAMPLES_1  = 0x00, // 1 sample (no internal averaging)
    SAMPLES_2  = 0x01, // 2 samples
    SAMPLES_4  = 0x02, // 4 samples (Recommended balance)
    SAMPLES_8  = 0x03, // 8 samples
    SAMPLES_16 = 0x04  // 16 samples (Max filtering, slower conversion)
};

/**
 * @brief Conversion operating mode.
 */
enum class ConversionMode : uint8_t {
    ONE_SHOT   = 0x00, // Manual trigger
    CONTINUOUS = 0x01  // Automatic background conversion
};

/// Bitfield representation of MAX31856 Status Register (0x0F)
struct FaultFlags {
    bool openCircuit        = false; // Thermocouple wire disconnected / open
    bool overUnderVoltage   = false; // Input voltage outside allowed range
    bool tcLow              = false; // Thermocouple temp below LTLF threshold
    bool tcHigh             = false; // Thermocouple temp above LTHF threshold
    bool cjLow              = false; // Cold junction temp below CJLF threshold
    bool cjHigh             = false; // Cold junction temp above CJHF threshold
    bool tcOutOfRange       = false; // Thermocouple temp outside physical sensor limits
    bool cjOutOfRange       = false; // Cold junction temp outside IC limits (-55°C to +125°C)
    uint8_t rawByte         = 0x00;  // Raw fault register byte

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
    float temperature      = 0.0f;   // Filtered thermocouple temperature in °C
    float rawTemperature   = 0.0f;   // Unfiltered raw thermocouple temperature in °C
    float coldJunction     = 0.0f;   // Cold junction internal IC temperature in °C
    FaultFlags fault;                // Decoded fault flags
    uint32_t timestampMs   = 0;      // Timestamp of reading (esp_timer_get_time() in ms)
    bool isValid           = false;  // True if reading is valid and verified
};

/// Comprehensive MAX31856 Configuration Structure
struct MAX31856Config {
    ThermocoupleType tcType   = ThermocoupleType::TYPE_K;
    NoiseFilter filter        = NoiseFilter::FILTER_50HZ;
    AveragingMode averaging   = AveragingMode::SAMPLES_4;
    ConversionMode mode       = ConversionMode::CONTINUOUS;

    float tcHighFaultTemp     = DEFAULT_TC_HIGH_FAULT_TEMP;
    float tcLowFaultTemp      = DEFAULT_TC_LOW_FAULT_TEMP;
    float cjHighFaultTemp     = DEFAULT_CJ_HIGH_FAULT_TEMP;
    float cjLowFaultTemp      = DEFAULT_CJ_LOW_FAULT_TEMP;

    bool emaFilterEnabled     = true;
    float emaAlpha            = DEFAULT_EMA_ALPHA;
    uint8_t faultStreakLimit  = DEFAULT_FAULT_STREAK_THRESHOLD;
};

/**
 * @brief Native C++ ESP-IDF v6.0.2 Driver for the MAX31856 Thermocouple IC.
 * 
 * Uses official Espressif driver/spi_master.h for hardware SPI communication.
 * Provides complete register control, multi-sample fault validation, and EMA filtering.
 */
class MAX31856 {
public:
    /**
     * @brief Construct a MAX31856 driver instance.
     * @param spiHost ESP-IDF SPI host device (e.g. SPI2_HOST or SPI3_HOST)
     * @param csPin Chip Select GPIO pin
     * @param config Configuration parameters
     */
    MAX31856(spi_host_device_t spiHost, gpio_num_t csPin, const MAX31856Config& config = MAX31856Config());

    /**
     * @brief Destroy the MAX31856 instance and remove device from SPI bus.
     */
    ~MAX31856();

    /**
     * @brief Initialize SPI device and write hardware registers.
     * @return true on success, false if SPI device initialization failed.
     */
    bool begin();

    /**
     * @brief Perform a sensor read cycle (Reads temperature, cold junction, and fault register).
     * @return SensorReading Structure with updated readings and fault flags.
     */
    SensorReading read();

    /**
     * @brief Get last cached sensor reading without performing an SPI transaction.
     * @return Const reference to the latest SensorReading.
     */
    const SensorReading& getLatest() const { return _latestReading; }

    /**
     * @brief Apply new runtime configuration (e.g., changing thermocouple type or fault thresholds).
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
     * @return true on success, false on communication error.
     */
    bool triggerOneShot();

    /**
     * @brief Clear MAX31856 internal hardware fault register.
     */
    void clearFaultRegister();

    /**
     * @brief Read a single 8-bit register from the MAX31856 IC.
     * @param regAddr Register address (0x00 - 0x0F).
     * @return 8-bit register value.
     */
    uint8_t readRegister(uint8_t regAddr);

    /**
     * @brief Write a single 8-bit value to a MAX31856 register.
     * @param regAddr Register address (0x00 - 0x0F).
     * @param value 8-bit value to write.
     * @return true on success, false on error.
     */
    bool writeRegister(uint8_t regAddr, uint8_t value);

private:
    spi_host_device_t   _spiHost;
    gpio_num_t          _csPin;
    spi_device_handle_t _spiHandle = nullptr;
    MAX31856Config      _config;

    SensorReading _latestReading;
    float         _filteredTemp = 25.0f;
    bool          _filterInit   = false;
    uint8_t       _faultStreak  = 0;

    void parseFaultRegister(uint8_t rawFault, FaultFlags& flags);
    void writeThresholds();
};

} // namespace sensor

