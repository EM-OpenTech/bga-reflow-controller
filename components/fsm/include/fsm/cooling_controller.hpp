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
 * @file cooling_controller.hpp
 * @brief Independent Cooling Phase, Fan Timing & Lamp Override Controller.
 *
 * Coordinates fan turn-on delays (fanCoolingDelayS), minimum active fan duration
 * (fanCoolingDurationS), passive natural cooling modes, dual-channel safe temperature
 * thresholds (coolingSafeTemp), and manual user overrides.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once

#include "config/machine_config.hpp"
#include "output/output_manager.hpp"
#include <cstdint>

namespace fsm {

/**
 * @class CoolingController
 * @brief Autonomous manager for cooling phase logic and peripheral overrides.
 */
class CoolingController {
public:
    /**
     * @brief Construct CoolingController.
     * @param outputs Reference to hardware OutputManager
     * @param settings Reference to loaded MachineSettings
     */
    CoolingController(output::OutputManager& outputs, const config::MachineSettings& settings);

    /**
     * @brief Enter cooling state: initiates fan delay timer and starts cooling lifecycle.
     * @param immediate If true, activates fan immediately regardless of fanCoolingDelayS (e.g. for emergency faults).
     */
    void startCooling(bool immediate = false);

    /**
     * @brief Periodic update during COOLING state.
     *
     * Evaluates fan delay, runs fan, and checks dual-channel safe temperature criteria.
     *
     * @param topTemp Current measured top temperature (°C)
     * @param bottomTemp Current measured bottom temperature (°C)
     * @param dtMs Elapsed time since last tick (ms)
     * @return true if cooling cycle is complete (both channels <= coolingSafeTemp and duration met)
     */
    bool update(float topTemp, float bottomTemp, uint32_t dtMs);

    /**
     * @brief Stop cooling cycle and turn off automatic fan.
     */
    void stopCooling();

    /**
     * @brief Emergency / Fault handler: keeps fan ON if temperatures exceed safe limits.
     * @param topTemp Current measured top temperature (°C)
     * @param bottomTemp Current measured bottom temperature (°C)
     */
    void handleFault(float topTemp, float bottomTemp);

    /**
     * @brief Set manual fan override state.
     * Valid across all system states (OR'd with automatic fan).
     * @param on true to force fan ON, false to release override
     */
    void setFanOverride(bool on);

    /**
     * @brief Set manual work lamp state.
     * Valid across all system states.
     * @param on true to force lamp ON, false to turn lamp OFF
     */
    void setLampOverride(bool on);

    /**
     * @brief Check effective status of cooling fan (auto OR manual override).
     */
    [[nodiscard]] bool isFanEffective() const { return _fanAuto || _fanOverride; }

    /**
     * @brief Check effective status of work lamp.
     */
    [[nodiscard]] bool isLampEffective() const { return _lampOverride; }

    /**
     * @brief Check if fan is automatically commanded by cooling profile.
     */
    [[nodiscard]] bool isFanAuto() const { return _fanAuto; }

    /**
     * @brief Update physical GPIOs for fan and lamp based on current effective states.
     */
    void applyOutputs();

private:
    output::OutputManager& _outputs;
    const config::MachineSettings& _settings;

    uint32_t _coolingElapsedMs = 0; ///< Total time in COOLING state (ms)
    uint32_t _fanRunMs = 0;         ///< Time fan has actively run (ms)
    bool _fanAuto = false;
    bool _fanOverride = false;
    bool _lampOverride = false;
};

} // namespace fsm
