/**
 * @file thermal_simulator.hpp
 * @brief Thermal Physics Simulator for BGA Reflow Controller.
 *
 * Simulates Top and Bottom heater thermal physics, heat capacity, dissipation
 * to ambient, and forced convection cooling by the fan.
 *
 * @author ESP-IDF Reflow Controller Team
 * @date 2026-09-23
 */

#pragma once

#include <cstdint>
#include <algorithm>

namespace sim {

/**
 * @class ThermalSimulator
 * @brief Thermal Physics Simulator for BGA Reflow Controller.
 * 
 * Simulates Top and Bottom heater physics, thermal mass, heat dissipation
 * to ambient (25°C), and active cooling by the fan.
 * Encapsulated in its own component so all sensor drivers remain clean.
 */
class ThermalSimulator {
public:
    static constexpr float AMBIENT_TEMP = 25.0f; ///< Ambient room temperature baseline (°C)

    /**
     * @brief Default constructor.
     */
    ThermalSimulator() = default;

    /**
     * @brief Resets simulated heater temperatures to an initial temperature.
     * @param initialTemp Starting temperature (°C), defaults to AMBIENT_TEMP (25°C).
     */
    void reset(float initialTemp = AMBIENT_TEMP) {
        _topTemp = initialTemp;
        _bottomTemp = initialTemp;
    }

    /**
     * @brief Step the thermal simulation by dtMs milliseconds.
     * 
     * @param topPower    Current Top Heater PID power (0.0 - 100.0%)
     * @param bottomPower Current Bottom Heater PID power (0.0 - 100.0%)
     * @param fanActive   True if cooling fan is running
     * @param dtMs        Delta time in milliseconds (e.g. 100ms)
     */
    void update(float topPower, float bottomPower, bool fanActive, uint32_t dtMs = 100) {
        float dtSec = static_cast<float>(dtMs) / 1000.0f;

        // ── Top Heater Thermal Model ─────────────────────────────────────────
        // Rise: ~3.5°C/s at 100% power
        float topRise = topPower * 0.035f;
        // Natural heat dissipation to room temperature
        float topLoss = (_topTemp - AMBIENT_TEMP) * 0.005f;
        // Forced convection by active fan
        float topFanCooling = fanActive ? ((_topTemp - AMBIENT_TEMP) * 0.02f) : 0.0f;

        _topTemp += (topRise - topLoss - topFanCooling) * dtSec;
        if (_topTemp < AMBIENT_TEMP) _topTemp = AMBIENT_TEMP;

        // ── Bottom Heater Thermal Model (Larger thermal mass) ─────────────────
        // Rise: ~1.5°C/s at 100% power (heavier quartz/ceramic mass)
        float botRise = bottomPower * 0.015f;
        float botLoss = (_bottomTemp - AMBIENT_TEMP) * 0.002f;
        float botFanCooling = fanActive ? ((_bottomTemp - AMBIENT_TEMP) * 0.005f) : 0.0f;

        _bottomTemp += (botRise - botLoss - botFanCooling) * dtSec;
        if (_bottomTemp < AMBIENT_TEMP) _bottomTemp = AMBIENT_TEMP;
    }

    /**
     * @brief Returns current simulated Top Heater temperature.
     * @return Temperature in °C.
     */
    float getTopTemperature() const { return _topTemp; }

    /**
     * @brief Returns current simulated Bottom Heater temperature.
     * @return Temperature in °C.
     */
    float getBottomTemperature() const { return _bottomTemp; }

    /**
     * @brief Sets simulated Top Heater temperature.
     * @param temp Temperature in °C.
     */
    void setTopTemperature(float temp) { _topTemp = temp; }

    /**
     * @brief Sets simulated Bottom Heater temperature.
     * @param temp Temperature in °C.
     */
    void setBottomTemperature(float temp) { _bottomTemp = temp; }

private:
    float _topTemp    = AMBIENT_TEMP;
    float _bottomTemp = AMBIENT_TEMP;
};

} // namespace sim
