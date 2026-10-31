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

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

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
// MAX31856 REGISTER BITMASKS
// ============================================================================

namespace Cr0Bits {
constexpr uint8_t CMODE = 0x80;       ///< Bit 7: Conversion mode (1 = Continuous, 0 = Normally Off)
constexpr uint8_t ONE_SHOT = 0x40;    ///< Bit 6: 1-Shot conversion trigger
constexpr uint8_t OCFAULT_0 = 0x10;   ///< Bit 4: Open-circuit fault check enable (01 = every conversion)
constexpr uint8_t FAULT_INTR = 0x04;  ///< Bit 2: Fault mode (1 = Interrupt, 0 = Comparator)
constexpr uint8_t FAULT_CLEAR = 0x02; ///< Bit 1: Fault clear
constexpr uint8_t FILTER_50HZ = 0x01; ///< Bit 0: Filter notch (1 = 50 Hz, 0 = 60 Hz)
} // namespace Cr0Bits

namespace SrBits {
constexpr uint8_t CJ_RANGE = 0x80;        ///< Bit 7: Cold junction temperature out of range
constexpr uint8_t TC_RANGE = 0x40;        ///< Bit 6: Thermocouple temperature out of range
constexpr uint8_t CJ_HIGH = 0x20;         ///< Bit 5: Cold junction high threshold fault
constexpr uint8_t CJ_LOW = 0x10;          ///< Bit 4: Cold junction low threshold fault
constexpr uint8_t TC_HIGH = 0x08;         ///< Bit 3: Thermocouple high threshold fault
constexpr uint8_t TC_LOW = 0x04;          ///< Bit 2: Thermocouple low threshold fault
constexpr uint8_t OVER_UNDER_VOLT = 0x02; ///< Bit 1: Overvoltage or undervoltage input fault
constexpr uint8_t OPEN_CIRCUIT = 0x01;    ///< Bit 0: Thermocouple open-circuit fault
} // namespace SrBits

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
    TYPE_K = 0x03, ///< Standard for Reflow Stations
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
    SAMPLES_1 = 0x00, ///< 1 sample (no internal averaging)
    SAMPLES_2 = 0x01, ///< 2 samples
    SAMPLES_4 = 0x02, ///< 4 samples (Recommended balance)
    SAMPLES_8 = 0x03, ///< 8 samples
    SAMPLES_16 = 0x04 ///< 16 samples (Max filtering, slower conversion)
};

/**
 * @brief Conversion operating mode.
 */
enum class ConversionMode : uint8_t {
    ONE_SHOT = 0x00,  ///< Manual trigger
    CONTINUOUS = 0x01 ///< Automatic background conversion
};

/// Bitfield representation of MAX31856 Status Register (0x0F)
struct FaultFlags {
    bool openCircuit = false;      ///< Thermocouple wire disconnected / open
    bool overUnderVoltage = false; ///< Input voltage outside allowed range
    bool tcLow = false;            ///< Thermocouple temp below LTLF threshold
    bool tcHigh = false;           ///< Thermocouple temp above LTHF threshold
    bool cjLow = false;            ///< Cold junction temp below CJLF threshold
    bool cjHigh = false;           ///< Cold junction temp above CJHF threshold
    bool tcOutOfRange = false;     ///< Thermocouple temp outside physical sensor limits
    bool cjOutOfRange = false;     ///< Cold junction temp outside IC limits (-55°C to +125°C)
    uint8_t rawByte = 0x00;        ///< Raw fault register byte

    /**
     * @brief Check if any hardware fault condition is active.
     * @return true if raw register byte is non-zero or any fault flag is set.
     */
    [[nodiscard]] bool hasFault() const {
        return rawByte != 0x00 || openCircuit || overUnderVoltage || tcLow || tcHigh || cjLow || cjHigh ||
               tcOutOfRange || cjOutOfRange;
    }
};

/// Structure holding the output of a single sensor read cycle
struct SensorReading {
    float temperature = 0.0f;    ///< Filtered thermocouple temperature in °C
    float rawTemperature = 0.0f; ///< Unfiltered raw thermocouple temperature in °C
    float coldJunction = 0.0f;   ///< Cold junction internal IC temperature in °C
    FaultFlags fault;            ///< Decoded fault flags
    uint32_t timestampMs = 0;    ///< Timestamp of reading (esp_timer_get_time() in ms)
    bool isValid = false;        ///< True if reading is valid and verified
};

/// Comprehensive MAX31856 Configuration Structure
struct MAX31856Config {
    ThermocoupleType tcType = ThermocoupleType::TYPE_K; ///< Thermocouple chemistry type
    NoiseFilter filter = NoiseFilter::FILTER_50HZ;      ///< Mains noise rejection filter (50Hz / 60Hz)
    AveragingMode averaging = AveragingMode::SAMPLES_4; ///< Internal hardware averaging mode
    ConversionMode mode = ConversionMode::CONTINUOUS;   ///< Continuous or one-shot conversion mode

    float tcHighFaultTemp = DEFAULT_TC_HIGH_FAULT_TEMP; ///< High thermocouple fault threshold (°C)
    float tcLowFaultTemp = DEFAULT_TC_LOW_FAULT_TEMP;   ///< Low thermocouple fault threshold (°C)
    float cjHighFaultTemp = DEFAULT_CJ_HIGH_FAULT_TEMP; ///< High cold-junction fault threshold (°C)
    float cjLowFaultTemp = DEFAULT_CJ_LOW_FAULT_TEMP;   ///< Low cold-junction fault threshold (°C)
    float cjOffset = 0.0f;                              ///< Cold junction calibration offset (-8.0°C to +7.9375°C)

    bool emaFilterEnabled = true;                       ///< Enable software EMA smoothing filter
    float emaAlpha = DEFAULT_EMA_ALPHA;                 ///< EMA alpha smoothing factor [0.0..1.0]
    uint8_t faultStreakLimit = DEFAULT_FAULT_STREAK_THRESHOLD; ///< Consecutive fault reads before declaring error
};

namespace detail {

/**
 * @brief Encode cold junction temperature threshold into signed 8-bit register representation (1°C/LSB).
 */
[[nodiscard]] inline uint8_t encodeCjThreshold(float tempC) {
    if (!std::isfinite(tempC)) {
        tempC = DEFAULT_CJ_HIGH_FAULT_TEMP;
    }
    const float clamped = std::clamp(tempC, -128.0f, 127.0f);
    return static_cast<uint8_t>(static_cast<int8_t>(std::round(clamped)));
}

/**
 * @brief Encode thermocouple temperature threshold into signed 16-bit register representation (0.0625°C/LSB).
 */
[[nodiscard]] inline int16_t encodeTcThreshold(float tempC) {
    if (!std::isfinite(tempC)) {
        tempC = DEFAULT_TC_HIGH_FAULT_TEMP;
    }
    const float clamped = std::clamp(tempC, -2048.0f, 2047.9375f);
    return static_cast<int16_t>(std::round(clamped * 16.0f));
}

/**
 * @brief Encode cold junction calibration offset into signed 8-bit register representation (0.0625°C/LSB).
 */
[[nodiscard]] inline int8_t encodeCjto(float offset) {
    if (!std::isfinite(offset)) {
        offset = 0.0f;
    }
    const float clamped = std::clamp(offset, -8.0f, 7.9375f);
    return static_cast<int8_t>(std::round(clamped * 16.0f));
}

/**
 * @brief Compute CR0 configuration byte from conversion mode and noise filter settings.
 */
[[nodiscard]] constexpr uint8_t computeCr0(ConversionMode mode, NoiseFilter filter) {
    uint8_t cr0 = Cr0Bits::OCFAULT_0; // Open Circuit check enabled (OCFAULT = 01)
    if (mode == ConversionMode::CONTINUOUS) {
        cr0 |= Cr0Bits::CMODE;
    }
    if (filter == NoiseFilter::FILTER_50HZ) {
        cr0 |= Cr0Bits::FILTER_50HZ;
    }
    return cr0;
}

/**
 * @brief Compute CR1 configuration byte from sample averaging and thermocouple type settings.
 */
[[nodiscard]] constexpr uint8_t computeCr1(AveragingMode averaging, ThermocoupleType tcType) {
    return static_cast<uint8_t>((static_cast<uint8_t>(averaging) << 4) | (static_cast<uint8_t>(tcType) & 0x0F));
}

/**
 * @brief Decode 19-bit signed thermocouple temperature from raw LTCB registers (0.0078125°C/LSB).
 */
[[nodiscard]] constexpr float decodeThermocoupleTemp(uint8_t ltcHigh, uint8_t ltcMid, uint8_t ltcLow) {
    int32_t rawTc = (static_cast<int32_t>(ltcHigh) << 16) | (static_cast<int32_t>(ltcMid) << 8) |
                    static_cast<int32_t>(ltcLow & 0xE0); // Mask reserved bits [4:0]
    if ((rawTc & 0x800000) != 0) {
        rawTc |= static_cast<int32_t>(0xFF000000UL);     // Sign extend 24-bit to 32-bit signed int
    }
    return static_cast<float>(rawTc >> 5) * 0.0078125f;
}

/**
 * @brief Decode 14-bit signed cold junction temperature from raw CJT registers (0.015625°C/LSB).
 */
[[nodiscard]] constexpr float decodeColdJunctionTemp(uint8_t cjHigh, uint8_t cjLow) {
    const int16_t rawCj = static_cast<int16_t>((static_cast<uint16_t>(cjHigh) << 8) | static_cast<uint16_t>(cjLow));
    return static_cast<float>(rawCj >> 2) * 0.015625f;
}

/**
 * @brief Calculate initial / 1-shot conversion duration in milliseconds based on Table 2 of MAX31856 datasheet.
 */
[[nodiscard]] constexpr uint32_t calculateConversionTimeMs(NoiseFilter filter, AveragingMode averaging) {
    const uint32_t baseMs = (filter == NoiseFilter::FILTER_50HZ) ? 169u : 143u;
    const uint32_t addMs = (filter == NoiseFilter::FILTER_50HZ) ? 40u : 34u;
    const uint32_t samples = 1u << static_cast<uint32_t>(averaging);
    return baseMs + ((samples - 1u) * addMs);
}

} // namespace detail

/**
 * @class MAX31856
 * @brief Precision hardware driver for MAX31856 thermocouple converter IC.
 *
 * Uses official Espressif driver/spi_master.h for hardware SPI communication.
 * Provides register control, multi-sample fault validation, and EMA filtering.
 *
 * @note **Single-Writer Architecture (No Internal Locking):**
 *       This driver contains NO mutexes, spinlocks, or critical sections.
 *       All methods — read() and all write/config methods — MUST be called exclusively
 *       from a single FreeRTOS task (control_task on Core 1).
 *
 *       Inter-task data sharing is handled at the AppController level via the
 *       lock-free SensorSnapshot mailbox. Other tasks (safety_task, web_task)
 *       consume the snapshot, never this class directly.
 *
 *       Do NOT add portENTER_CRITICAL() here — it disables all interrupts on the
 *       calling core, blocking the FreeRTOS tick and risking TG0WDT_SYS_RST on
 *       ESP32-S3 with Octal PSRAM (cache ops + disabled interrupts = deadlock).
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

    MAX31856(const MAX31856&) = delete;
    MAX31856& operator=(const MAX31856&) = delete;
    MAX31856(MAX31856&&) = delete;
    MAX31856& operator=(MAX31856&&) = delete;

    /**
     * @brief Initialize SPI device and write hardware registers.
     *
     * When `skipPriming` is false (default), begin() blocks the calling task for
     * conversionTimeMs() + 50 ms to wait for the first IC conversion to complete,
     * then calls read() once to warm up the EMA filter.
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
     * @brief Initialize all hardware registers and clear fault flags (Boot time & recovery).
     * @param verbose When true, log POST diagnostics and failures; when false, suppress failure spam.
     * @return true on success, false on communication error.
     */
    bool initHardware(bool verbose = true);

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
    [[nodiscard]] uint32_t conversionTimeMs() const;

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
    spi_host_device_t _spiHost;               ///< ESP-IDF SPI host identifier
    gpio_num_t _csPin;                        ///< Dedicated Chip Select GPIO pin
    spi_device_handle_t _spiHandle = nullptr; ///< ESP-IDF SPI device handle
    MAX31856Config _config;                   ///< Active sensor configuration
    float _filteredTemp = 25.0f;              ///< Running exponential moving average temperature (°C)
    bool _filterInit = false;                 ///< Initialization flag for EMA filter seed
    uint8_t _faultStreak = 0;                 ///< Consecutive fault counter for noise suppression
    bool _faultLogged = false;                ///< Flag ensuring persistent fault is logged only once
    bool _porFaultLogged = false;             ///< Flag ensuring persistent POR / bus failure is logged only once

    static void parseFaultRegister(uint8_t rawFault, FaultFlags& flags);

    /**
     * @brief Check whether the IC underwent an unexpected Power-On Reset or brownout.
     * @param rCr0 Value read from CR0 register (0x00).
     * @param rMask Value read from MASK register (0x02).
     * @return true if IC reset to defaults (CMODE=0 or MASK=0xFF).
     */
    [[nodiscard]] bool isPorCondition(uint8_t rCr0, uint8_t rMask) const;

    /**
     * @brief Perform recovery and silent background re-initialization following a POR event.
     * @param rCr0 Value read from CR0 register.
     * @param rMask Value read from MASK register.
     */
    void handlePorRecovery(uint8_t rCr0, uint8_t rMask);

    /**
     * @brief Log recovery notification once hardware communication is restored after a brownout.
     */
    void notifyPorRestoredIfPending();

    /**
     * @brief Process status register faults with deglitching and streak capping.
     *
     * Implements Dual-Layer Fault Reporting (IEC 60730 Rationale):
     * - Status flags (reading.fault.*) are updated immediately on every read.
     * - reading.isValid is latched false only after faultStreakLimit consecutive faults,
     *   preventing transient EMI spikes from tripping the controller.
     * - Note: clearFaultRegister() is intentionally omitted here to prevent CR0
     *   corruption during 0xFF floating bus glitches (Comparator Mode auto-clears).
     *
     * @param rawFault Raw fault status register byte (REG_SR 0x0F).
     * @return true if reading is within deglitch tolerance, false if error streak tripped.
     */
    bool processFaultStreak(uint8_t rawFault);

    /**
     * @brief Update the running Exponential Moving Average (EMA) temperature filter.
     * @param rawTemp Unfiltered thermocouple temperature in °C.
     * @param rawFault Raw fault byte (used to prevent seeding from a faulted 0°C reading).
     * @return Filtered temperature in °C.
     */
    float updateEmaFilter(float rawTemp, uint8_t rawFault);

    /**
     * @brief Check whether a raw temperature value is within physically sane boundaries.
     * @param temp Temperature in °C.
     * @return true if finite and within [-100°C, 1850°C].
     */
    [[nodiscard]] static bool isTemperatureSane(float temp);
    bool writeThresholds();
    bool writeCjtoOffset(float offset);
};

} // namespace sensor
