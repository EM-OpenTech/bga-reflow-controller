# ESP32-S3 N16R8 DIY BGA Reflow Controller Firmware

> [!CAUTION]
>
> ### ⚠️ EXTREME DANGER: DIY HARDWARE, MAINS VOLTAGE & FIRE HAZARD
>
> This project is designed to control high-power heating elements and operates with **dangerous AC mains voltages (110V / 230V)**.
>
> * **USE AT YOUR OWN RISK:** Using mains electricity and high-temperature heaters carries a **severe risk of fire, severe burns, personal injury, and death**.
> * **DO NOT RELY ON SOFTWARE SAFETY MECHANISMS:** Although comprehensive software safety watchdogs (thermal runaway detection, stuck SSR detection, sensor fault monitoring) are implemented, **you must NEVER rely solely on software for protection**. Software can crash, hang, or contain bugs.
> * **MANDATORY HARDWARE SAFEGUARDS:** You **must** install independent, physical, non-resettable hardware protection mechanisms (e.g., thermal cutoffs / thermal fuses in series with heaters, properly rated mains fuses, emergency cut-off switches, and proper protective earth grounding).
> * **NO LIABILITY:** The author assumes absolutely no liability for property damage, fire, injury, or loss of life resulting from building, modifying, testing, or operating this system. The software and designs are provided strictly **"AS IS"** without warranties of any kind.
> * **Please conduct the first tests using a test setup, such as a 24V 3D printing hotend or a ceramic power resistor (driven via a DC-DC SSR or MOSFET module). If you don't know how to set this up, please do not conduct any tests at this stage of development**
>
---

## ℹ️ About This Project & Public Preview Status

* **Single-Person Hobby Project:** This is an open-source hobby project created and maintained by a **single private individual** (not a company, organization, or team).
* **AI-Assisted Development:** The architecture, code, and technical research were developed and designed to the best of my knowledge with the active assistance of AI tools.
* **Early Public Preview:** Due to community interest and requests from fellow makers, the codebase is published early to allow like-minded enthusiasts to inspect the code and conduct initial experiments.
* **Pending Hardware Real-World Tests:** The author is currently waiting for ordered hardware components to arrive to perform full real-world physical bench testing. Adjustments, tuning, and bugfixes will definitely follow in upcoming commits.
* **Source File Headers Notice:** Please ignore any placeholder or inconsistent author mentions in individual file headers (such as references to an automated "team") for now. These will be cleaned up and unified prior to the official v1.0.0 release.
**The legal open-source baseline is fully established via [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).**
* **Documentation & Architecture Guides:** Comprehensive step-by-step module architecture breakdowns and detailed documentation are actively work-in-progress and will be published sequentially.

---

## 🎯 Overview & Key Features

This open-source firmware turns an **ESP32-S3 (N16R8)** into a high-precision, dual-channel PID reflow controller designed for **custom DIY BGA Rework Stations** (e.g., GPU reballing/ replacement, CPU socket swaps, complex SMD soldering, and board repairs) as well as reflow ovens and hotplates.

* **Dual-Zone Independent PID Control:** Simultaneous, independent control for Top Heater (Dark Infrared) and Bottom Preheater with zero-cross SSR burst firing (1% duty-cycle resolution).
* **High-Precision MAX31856 SPI Thermocouples:** Optimized for Type-K thermocouples with native cold-junction compensation, noise filtering, and hardware fault detection.
* **Integrated Web Interface & Real-Time Graphing:** Embedded HTTP & WebSocket server streaming live temperature curves, setpoints, and heater power visualized with **Chart.js** (works 100% offline).
* **Multi-Stage Reflow Profile Designer:** Create, test, validate, and save custom soldering and preheat temperature profiles directly in the browser.
* **Dual Inflection-Point PID Autotuner:** Integrated `sTune` & `sTan` autotuning engine to calculate optimal PID gains for both heating zones.
* **Multi-Tier Safety Watchdogs:** Over-temperature limits, thermal runaway detection, open-circuit thermocouple monitoring, and stuck-SSR alarms.
* **Dual-Core FreeRTOS Architecture:** Native ESP-IDF v6.0.2 C++20 architecture with safety and control pinned to Core 1 and Wi-Fi / Web UI handled on Core 0.
* **The Most Frequently Asked Question** Since I’ve been asked several times whether more than two heating zones can be controlled separately, the answer is yes—in principle, it would be possible, since the ESP32-S3 N16R8 has more than enough processing power and memory. However, no approach or design concept has been developed for this yet. In the future, once the fundamentals are in place, this additional feature can possibly be implemented without any problems.

---

## 🔌 Hardware Pin Configuration

> [!IMPORTANT]
> The default GPIO pin assignments below represent a baseline reference design for the **ESP32-S3 (N16R8)**.
> The pin assignments were chosen randomly at the time of the first compilation **and should be adjusted to match the actual hardware capabilities if necessary.**

### Editing Pin Definitions

To change the GPIO mapping for your needs, edit the configuration header:
📁 [`components/config/include/config/pin_config.hpp`](components/config/include/config/pin_config.hpp)

After modifying any pin definitions, recompile and flash the firmware.
I'm using the ESP IDF version 6.0.2

### Reference Pin Assignment

| Function | Pin (ESP32-S3) | Description |
| :--- | :--- | :--- |
| **SPI SCK** | `GPIO 12` | SPI Clock for MAX31856 Thermocouple ICs |
| **SPI MISO** | `GPIO 13` | SPI Master In Slave Out (Data from MAX31856) |
| **SPI MOSI** | `GPIO 11` | SPI Master Out Slave In (Data to MAX31856) |
| **CS Top Heater** | `GPIO 10` | Chip Select for Top Thermocouple MAX31856 |
| **CS Bottom Heater** | `GPIO 9` | Chip Select for Bottom Thermocouple MAX31856 |
| **SSR Top Heater** | `GPIO 4` | Zero-Cross Solid State Relay (Top Heating Element) |
| **SSR Bottom Heater** | `GPIO 5` | Zero-Cross Solid State Relay (Bottom Heating Element) |
| **Cooling Fan** | `GPIO 6` | Auxiliary Cooling Fan Output |
| **Inspection Lamp** | `GPIO 7` | Auxiliary Work Lamp Output |
| **Buzzer** | `GPIO 15` | Acoustic Signal Buzzer Output |
| **Start Button** | `GPIO 1` | Front-Panel Momentary Start Push-Button |
| **Stop Button** | `GPIO 2` | Front-Panel Momentary Stop / Abort Push-Button |
| **Fan Switch** | `GPIO 3` | Manual Fan Override Latching Toggle Switch |
| **Lamp Switch** | `GPIO 8` | Manual Lamp Override Latching Toggle Switch |

---

## 📶 First Connection & Web UI

> Note: This is a single, complete 16 MB firmware image.

1. First read the Quick Start Guide [Flash-Firmware.md](Flash-Firmware.md)
2. Power on the ESP32-S3. It will host its own Wi-Fi Access Point:
   * **SSID:** `BGA Reflow Controller`
   * **Password:** `reflow123`
3. Connect with your smartphone, tablet, or PC.
4. Open your browser and navigate to: **`http://192.168.4.1`** (or `http://reflow.local`)

---

## 📜 License

Copyright (C) 2026  EM-OpenTech

This project is licensed under the **GNU Affero General Public License v3.0 (AGPLv3)** –
see the [LICENSE](LICENSE) file for the full license text.

> Under the AGPLv3, anyone who interacts with this software over a network
> (e.g. via the built-in web interface) is entitled to receive the
> corresponding source code.

For third-party libraries, bundled assets, and managed components, see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
