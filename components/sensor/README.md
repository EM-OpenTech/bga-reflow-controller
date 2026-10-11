# Precision MAX31856 Thermocouple Sensor Driver Component (C++20)

High-reliability, deterministic hardware driver for the **Maxim Integrated / Analog Devices MAX31856** precision thermocouple converter IC, operating natively on **ESP-IDF v6.0.2** and **FreeRTOS** for the **BGA Reflow Controller**.

This component manages hardware SPI bus communication, dual-channel temperature sensing (Top Heater and Bottom Heater), cold junction compensation, hardware mains notch filtering, software exponential moving average (EMA) smoothing, multi-sample fault deglitching, and automatic brownout / Power-On Reset (POR) recovery.

---

## 1. Overview & Architecture

Accurate, low-latency, and noise-immune temperature sensing is the safety-critical foundation of a BGA rework station. High-power SSR burst-fire switching, electromagnetic interference (EMI), and thermocouple wire vibrations introduce electrical noise. The `sensor` component abstracts the MAX31856 into a robust, deterministic sensing layer:

```
    [ Physical K-Type Thermocouples & Cold Junction Sensors ]
                                 |
                                 v
    +-------------------------------------------------------------------------+
    |                    Dual MAX31856 Hardware ICs                          |
    |  - 19-Bit Delta-Sigma ADC (0.0078125 °C / LSB resolution)               |
    |  - 14-Bit Cold Junction Sensor (0.015625 °C / LSB resolution)           |
    |  - 50 Hz Mains Notch Filter & 4-Sample Internal Hardware Averaging       |
    |  - Hardware Comparator Fault Detection (Open-Circuit, Over/Undervoltage) |
    +-------------------------------------------------------------------------+
                                 |
              [ Hardware SPI Bus @ 200 kHz (SPI Mode 1) ]
                                 |
                                 v
    +-------------------------------------------------------------------------+
    |                            sensor::MAX31856                             |
    |  - Dedicated Driver Instance per Channel (Top / Bottom)                 |
    |  - Single-Writer Task Isolation (Control Task on Core 1 @ 5 Hz / 200 ms)|
    |  - 16-Byte Atomic Burst Read (Registers 0x00..0x0F)                    |
    |  - Power-On Reset (POR) / Brownout Auto-Detection & Self-Healing        |
    |  - Multi-Sample Fault Deglitching (_faultStreak Overflow Capped)       |
    |  - Dual-Layer Safety Reporting (Immediate Fault Flags + isValid Latch)  |
    |  - Safe EMA Filter Seeding (Guards against 0 °C Open-Circuit Baseline)  |
    |  - Post-Configuration Readback Verification (verifyConfig)              |
    +-------------------------------------------------------------------------+
                                 |
                  [ Lock-Free SensorSnapshot Mailbox ]
                                 |
            +--------------------+--------------------+
            |                                         |
            v                                         v
   [ SafetyWatchdog Task ]                [ Web Server / REST / WS ]
     (Core 1, High Priority)                   (Core 0, Async UI)
```

### Single-Writer Lock-Free Architecture
* **No Internal Mutexes or Spinlocks:** The driver class contains no FreeRTOS critical sections (`portENTER_CRITICAL`) or mutex locks. Entering critical sections on the ESP32-S3 blocks ticks and can trigger watchdog panics (`TG0WDT_SYS_RST`) when paired with Octal PSRAM cache operations.
* **Core 1 Single-Writer:** All sensor driver calls (`read()`, `applyConfig()`, SPI transactions) are executed exclusively by the Core 1 `control_task` at 5 Hz (every 200 ms).
* **Cross-Core Data Distribution:** Inter-task reads (by `safety_task` on Core 1 or `web_task` on Core 0) access telemetry through the lock-free `SensorSnapshot` atomic mailbox provided by `AppController`.

---

## 2. Hardware Interface & ESP32-S3 Integration

### SPI Bus Specifications
* **Protocol:** SPI Mode 1 (CPOL = 0, CPHA = 1), MSB-first.
* **Bus Speed:** 200 kHz (`DEFAULT_MAX31856_SPI_SPEED_HZ`). Operating at 200 kHz provides maximum immunity against long thermocouple lead wiring capacitance, ground bounce, and EMI from neighboring 230V AC heating elements.
* **Timing & Delays:**
  * `cs_ena_posttrans = 1`: Guarantees 1 SPI bit-cycle ($\approx 5\,\mu\text{s}$ at 200 kHz), safely satisfying the MAX31856 chip select hold time requirement ($t_\text{CH} = 100\,\text{ns}$).
  * `input_delay_ns = 80`: Matches the maximum MAX31856 propagation delay ($t_\text{CDD} = 80\,\text{ns}$).

> [!WARNING]
> **ESP32-S3 N16R8 Octal PSRAM Hardware Conflict Guard:**  
> On ESP32-S3 modules equipped with 8 MB Octal PSRAM (N16R8), **GPIOs 35 through 42 are permanently reserved** for the internal high-speed Octal SPI RAM bus. Assigning any SPI CS, SCLK, MOSI, or MISO signals to pins 35–42 will cause instant system crashes or memory corruption. The hardware design exclusively assigns SPI bus pins outside this range.

---

## 3. Register Control & Temperature Conversion

### Register Map Summary

| Address | Name | Access | Description |
| :---: | :---: | :---: | :--- |
| `0x00` | `CR0` | R/W | Configuration 0 (Conversion Mode, One-Shot, Open-Circuit Test, Fault Mode, Filter Notch) |
| `0x01` | `CR1` | R/W | Configuration 1 (Averaging Mode bits 6:4, Thermocouple Type bits 3:0) |
| `0x02` | `MASK` | R/W | Fault Mask Register (`0x00` = all hardware faults unmasked) |
| `0x03` | `CJHF` | R/W | Cold-Junction High Fault Threshold ($1\,^\circ\text{C}$ / LSB) |
| `0x04` | `CJLF` | R/W | Cold-Junction Low Fault Threshold ($1\,^\circ\text{C}$ / LSB) |
| `0x05 - 0x06` | `LTHFTH / LTHFTL` | R/W | Linearized Thermocouple High Fault Threshold ($0.0625\,^\circ\text{C}$ / LSB) |
| `0x07 - 0x08` | `LTLFTH / LTLFTL` | R/W | Linearized Thermocouple Low Fault Threshold ($0.0625\,^\circ\text{C}$ / LSB) |
| `0x09` | `CJTO` | R/W | Cold-Junction Temperature Offset ($\pm 8.0\,^\circ\text{C}$, $0.0625\,^\circ\text{C}$ / LSB) |
| `0x0A - 0x0B` | `CJTH / CJTL` | R | Cold-Junction Temperature Data (14-bit signed, $0.015625\,^\circ\text{C}$ / LSB) |
| `0x0C - 0x0E` | `LTCBH / LTCBM / LTCBL` | R | Linearized TC Temperature Data (19-bit signed, $0.0078125\,^\circ\text{C}$ / LSB) |
| `0x0F` | `SR` | R | Fault Status Register (Decoded error flags) |

### 19-Bit Thermocouple Conversion Math
Thermocouple readings span three registers: `LTCBH` (bits 23:16), `LTCBM` (bits 15:8), and `LTCBL` (bits 7:0).
1. Bits 23..5 form a 19-bit signed integer. Bits 4:0 of `LTCBL` are reserved by the IC and masked out (`& 0xE0`).
2. Sign extension from 24-bit to 32-bit signed integer is performed if bit 23 is set.
3. The result is shifted right by 5 and multiplied by $0.0078125\,^\circ\text{C}$ ($1/128\,^\circ\text{C}$ per LSB):
   $$\text{Temp}_\text{TC} = \left(\text{rawTc} \gg 5\right) \times 0.0078125$$

### 14-Bit Cold Junction Conversion Math
Cold junction readings span two registers: `CJTH` (bits 15:8) and `CJTL` (bits 7:0).
1. Bits 15..2 form a 14-bit signed integer. Bits 1:0 of `CJTL` are reserved.
2. The 16-bit word is shifted right by 2 and multiplied by $0.015625\,^\circ\text{C}$ ($1/64\,^\circ\text{C}$ per LSB):
   $$\text{Temp}_\text{CJ} = \left(\text{rawCj} \gg 2\right) \times 0.015625$$

### Cold Junction Offset Calibration (`CJTO`)
The IC supports hardware-level cold junction offset injection via register `0x09` (`CJTO`). Clamped between $-8.0\,^\circ\text{C}$ and $+7.9375\,^\circ\text{C}$, it allows calibrating PCB thermal gradients directly inside the IC's internal DSP math:
$$\text{CJTO}_\text{raw} = \text{round}\left(\text{clamp}(\text{offset}, -8.0, 7.9375) \times 16.0\right)$$

---

## 4. Signal Conditioning & Fault Robustness

### 4.1 Digital Filtering Pipeline
1. **50 Hz Mains Notch Filter:** Configured in `CR0` bit 0 to reject standard European $50\,\text{Hz}$ power line hum and harmonics.
2. **Internal 4-Sample Hardware Averaging:** Configured in `CR1` bits 6:4 (`AveragingMode::SAMPLES_4`), providing hardware-level oversampling.
3. **Exponential Moving Average (EMA) Smoothing:** Software filter smoothing rapid thermal fluctuations:
   $$y_k = \alpha \cdot x_k + (1 - \alpha) \cdot y_{k-1} \quad (\text{Default } \alpha = 0.3)$$
   * **Safe Seeding Guard:** The filter will **never seed from 0 °C** during startup if the initial read reports a hardware fault (e.g. open thermocouple). It leaves `_filterInit = false` until the first clean, fault-free reading arrives, preventing slow convergence recovery artifacts.

### 4.2 Multi-Sample Fault Deglitching (`_faultStreak`)
Thermal couples running near high-current SSR switching circuits can experience transient electromagnetic spikes lasting a single ADC conversion cycle.
* **Deglitching Window:** A configurable streak counter (`faultStreakLimit`, default: 3 reads / $600\,\text{ms}$) filters out spurious glitches.
* **Dual-Layer Reporting:**
  * `reading.fault.*`: Individual fault bits are **always decoded and reported immediately** on every cycle.
  * `reading.isValid`: Flips from `true` to `false` only after `_faultStreak >= faultStreakLimit` consecutive cycles fail.
* **Counter Overflow Capping:** The streak counter is explicitly capped at `faultStreakLimit`. Without capping, an 8-bit counter wraps from 255 to 0 after ~25.5 seconds, which would falsely report `isValid = true` on a permanently broken thermocouple.

### 4.3 IC Power-On Reset (POR) / Brownout Self-Healing
Every 200 ms read cycle executes an **atomic 16-byte burst read** (`0x00` through `0x0F`):
* If the MAX31856 experiences a brief power glitch or brownout, its internal state resets: `CR0` reverts to `0x00` (`CMODE = 0`, halting conversions) and `MASK` reverts to `0xFF`.
* In this brownout state, the temperature registers freeze at $0.0\,^\circ\text{C}$ without setting any fault bits in `SR`!
* The driver immediately detects this condition:
  ```cpp
  bool isPorReset = (_config.mode == ConversionMode::CONTINUOUS && (rCr0 & (1 << 7)) == 0) || (rMask == 0xFF);
  ```
* Upon detection, the reading is marked invalid (`isValid = false`), the EMA baseline is reset, and `initHardware()` automatically reprograms all registers, restoring continuous conversion on the fly.

### 4.4 Hardware Readback Verification (`verifyConfig`)
After boot (`initHardware`) and after every runtime reconfiguration (`applyConfig`), the driver performs a 10-byte burst readback (`CR0` through `CJTO`). It verifies that `CR0`, `CR1`, `MASK`, `CJTO`, and all high/low threshold registers match their target values before reporting success.

---

## 5. Public API Reference

Defined in `sensor::MAX31856` ([`components/sensor/include/sensor/max31856.hpp`](include/sensor/max31856.hpp)):

### Core Driver Class

| Method | Return Type | Description |
| :--- | :---: | :--- |
| `MAX31856(spiHost, csPin, config)` | Constructor | Instantiates driver for a specific SPI host and CS GPIO pin. |
| `~MAX31856()` | Destructor | Unregisters SPI device handle and frees SPI bus resources. |
| `begin(skipPriming = false)` | `bool` | Initializes SPI bus interface, programs registers, and primes filter. |
| `read()` | `SensorReading` | Executes 16-byte burst read, decodes temperatures, checks faults, and updates EMA. |
| `initHardware()` | `bool` | Programs registers `CR0..CJTO` and unmasks hardware faults. |
| `applyConfig(config)` | `bool` | Applies runtime configuration changes and verifies registers via readback. |
| `resetFilter(initialTemp)` | `void` | Resets EMA filter state and preloads it with a specific temperature. |
| `triggerOneShot()` | `bool` | Sets `1SHOT` bit in `CR0` to trigger an on-demand conversion. |
| `clearFaultRegister()` | `void` | Clears latched hardware fault bits (in interrupt mode). |
| `conversionTimeMs()` | `uint32_t` | Calculates required first-conversion time in ms for active filter & averaging settings. |
| `readRegister(regAddr)` | `uint8_t` | Reads a single 8-bit register over SPI. |
| `readRegisters(startReg, buf, len)` | `bool` | Reads consecutive registers in a single atomic SPI burst transaction. |
| `writeRegister(regAddr, value)` | `bool` | Writes a single 8-bit value to a register over SPI. |
| `verifyConfig(cr0, cr1, cjto)` | `bool` | Reads back and validates all configuration and threshold registers. |

### Data Structures & Enums

* **`ThermocoupleType`:** `TYPE_K` (default), `TYPE_B`, `TYPE_E`, `TYPE_J`, `TYPE_N`, `TYPE_R`, `TYPE_S`, `TYPE_T`.
* **`NoiseFilter`:** `FILTER_50HZ` (default, European mains), `FILTER_60HZ`.
* **`AveragingMode`:** `SAMPLES_1`, `SAMPLES_2`, `SAMPLES_4` (default), `SAMPLES_8`, `SAMPLES_16`.
* **`ConversionMode`:** `CONTINUOUS` (default), `ONE_SHOT`.
* **`FaultFlags`:** Decoded hardware fault states:
  * `openCircuit`: Thermocouple wire disconnected or open.
  * `overUnderVoltage`: Input voltage outside valid rails.
  * `tcLow` / `tcHigh`: TC temperature exceeded threshold limits.
  * `cjLow` / `cjHigh`: Cold junction temperature exceeded threshold limits.
  * `tcOutOfRange` / `cjOutOfRange`: Sensor value exceeded physical operating limits.
* **`SensorReading`:**
  * `temperature`: Filtered thermocouple temperature in °C.
  * `rawTemperature`: Unfiltered thermocouple temperature in °C.
  * `coldJunction`: Cold junction IC temperature in °C.
  * `fault`: Decoded `FaultFlags` structure.
  * `timestampMs`: Timestamp of acquisition (`esp_timer_get_time()` in ms).
  * `isValid`: `true` if reading is valid and verified.

---

## 6. C++20 Usage Example

### Parallel Multi-Sensor Initialization Pattern
To avoid accumulating serial priming delays ($2 \times 339\,\text{ms} \approx 680\,\text{ms}$ at boot), use the parallel startup pattern:

```cpp
#include "sensor/max31856.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "SENSOR_DEMO";

void sensor_task(void* pvParameters)
{
    // 1. Configure sensor parameters
    sensor::MAX31856Config cfg;
    cfg.tcType           = sensor::ThermocoupleType::TYPE_K;
    cfg.filter           = sensor::NoiseFilter::FILTER_50HZ;
    cfg.averaging        = sensor::AveragingMode::SAMPLES_4;
    cfg.emaAlpha         = 0.3f;
    cfg.faultStreakLimit = 3;

    // 2. Instantiate dual-channel sensors
    sensor::MAX31856 topSensor(SPI2_HOST, GPIO_NUM_10, cfg);
    sensor::MAX31856 botSensor(SPI2_HOST, GPIO_NUM_11, cfg);

    // 3. Parallel hardware init (skipping priming delays)
    if (!topSensor.begin(true) || !botSensor.begin(true)) {
        ESP_LOGE(TAG, "Sensor hardware initialization failed!");
        vTaskDelete(nullptr);
    }

    // 4. Wait once for the longest conversion cycle (289 ms + 50 ms margin = 339 ms)
    uint32_t waitMs = std::max(topSensor.conversionTimeMs(), botSensor.conversionTimeMs()) + 50;
    vTaskDelay(pdMS_TO_TICKS(waitMs));

    // 5. Prime initial reading & EMA baseline
    topSensor.read();
    botSensor.read();

    ESP_LOGI(TAG, "Sensors initialized and primed successfully.");

    // 6. Cyclic 5 Hz (200 ms) Control Loop
    TickType_t lastWakeTime = xTaskGetTickCount();
    while (true) {
        sensor::SensorReading topReading = topSensor.read();
        sensor::SensorReading botReading = botSensor.read();

        if (topReading.isValid && botReading.isValid) {
            ESP_LOGI(TAG, "Top: %.1f°C (CJ: %.1f°C) | Bottom: %.1f°C (CJ: %.1f°C)",
                     topReading.temperature, topReading.coldJunction,
                     botReading.temperature, botReading.coldJunction);
        } else {
            if (topReading.fault.openCircuit) {
                ESP_LOGE(TAG, "CRITICAL: Top thermocouple open-circuit!");
            }
            if (botReading.fault.openCircuit) {
                ESP_LOGE(TAG, "CRITICAL: Bottom thermocouple open-circuit!");
            }
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(200));
    }
}
```

---

## 7. Unit Tests & Verification

The component includes 20 comprehensive Unity unit tests in [`test/test_sensor.cpp`](test/test_sensor.cpp) covering data structures, digital filtering, register math, deglitching, and fault handling:

| Test Case | Category | Description |
| :--- | :---: | :--- |
| `test_sensor_config_defaults` | Configuration | Verifies default Type-K, 50 Hz filter, $\alpha=0.3$, and streak limit of 3. |
| `test_sensor_reading_defaults` | Data Model | Confirms clean zero-initialization and `isValid = false` on fresh readings. |
| `test_sensor_ema_math` | Signal Processing | Validates discrete EMA equation $y_k = \alpha x_k + (1-\alpha) y_{k-1}$. |
| `test_sensor_ema_step_response_convergence` | Signal Processing | Tests step response convergence from 25 °C to 200 °C over 30 cycles. |
| `test_sensor_cjto_temperature_offset_math` | Calibration | Verifies arithmetic of applied calibration offset on temperature channels. |
| `test_sensor_fault_flag_isolation` | Fault Handling | Asserts individual fault flags set independently without cross-talk. |
| `test_sensor_fault_flags_has_fault` | Fault Handling | Verifies `hasFault()` returns `true` for any non-zero fault status. |
| `test_sensor_temperature_decoding_math` | Register Math | Validates 19-bit signed integer decoding and negative temp resolution. |
| `test_sensor_cjto_register_encoding_math` | Register Math | Verifies `CJTO` register clamping and 4-bit fractional rounding. |
| `test_sensor_fault_streak_deglitch` | Robustness | Asserts single-cycle EMI glitches do not invalidate active readings. |
| `test_sensor_ema_noise_spike_attenuation` | Signal Processing | Confirms 50 °C noise spikes are attenuated by the EMA filter. |
| `test_sensor_register_calculation_math` | Register Math | Tests CR0 and CR1 bitfield packing for various sensor chemistries. |
| `test_sensor_get_latest_copy_semantics` | Concurrency | Asserts safe copy semantics for latest cached readings. |
| `test_sensor_cjhf_cjlf_encoding` | Register Math | Verifies signed 8-bit encoding and clamping for cold junction thresholds. |
| `test_sensor_tc_threshold_encoding` | Register Math | Validates 16-bit signed encoding and clamping for thermocouple limits. |
| `test_sensor_ltcbl_reserved_bits_masking` | Register Math | Asserts reserved bits 4:0 in `LTCBL` are masked out before assembly. |
| `test_sensor_conversion_time_ms` | Timing | Verifies Table 2 conversion time calculations for all filter/sample modes. |
| `test_sensor_ema_not_seeded_on_fault` | Bugfix Coverage | Confirms EMA filter will not seed from 0 °C on a faulted startup read. |
| `test_sensor_fault_streak_no_overflow` | Bugfix Coverage | Proves streak counter capping prevents 8-bit overflow on permanent faults. |
| `test_sensor_por_brownout_detection` | Bugfix Coverage | Validates IC Power-On Reset / Brownout detection when CR0/MASK reset. |

---

## 8. CMake Integration

Defined in [`CMakeLists.txt`](CMakeLists.txt):

```cmake
idf_component_register(
    SRCS "max31856.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos driver esp_driver_spi esp_driver_gpio esp_timer
    PRIV_REQUIRES log
)
```

---

## 9. 📜 License

Copyright (C) 2026 EM-OpenTech

This component is part of the BGA Reflow Controller project and is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)**.

* **SPDX-FileCopyrightText:** `2026 EM-OpenTech`
* **SPDX-License-Identifier:** `AGPL-3.0-or-later`

See the project root [`LICENSE`](../../LICENSE) file for the full license text.
