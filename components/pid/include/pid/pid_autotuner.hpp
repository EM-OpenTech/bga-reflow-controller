/**
 * @file pid_autotuner.hpp
 * @brief High-level C++ wrapper for sTune PID autotuner in ESP-IDF v6.0.2.
 *
 * Wraps sTune open-loop inflection point autotuner for ceramic and quartz
 * infrared heaters. Configured to run deterministically from the 100ms
 * FreeRTOS control task loop on Core 1.
 *
 * @author ESP-IDF Reflow Controller Team
 * @date 2026-09-23
 */

#pragma once

#include <cstdint>
#include "sTune.h"
#include "pid_controller.hpp"

namespace pid {

/**
 * @brief High-level C++ wrapper for sTune PID autotuner in ESP-IDF v6.0.2.
 *
 * Wraps sTune open-loop inflection point autotuner for ceramic & quartz
 * infrared heaters. Configured to run deterministically from the 100ms
 * FreeRTOS control task loop.
 */
class PidAutotuner {
public:
    /**
     * @brief Construct a new PidAutotuner instance.
     */
    PidAutotuner();

    /**
     * @brief Default destructor.
     */
    ~PidAutotuner() = default;

    /**
     * @brief Start an autotuning run for the given heater channel.
     * @param isTop true for Top heater, false for Bottom heater
     * @param targetTemp Target temperature (°C)
     * @param startTemp Current measured temperature at test start (°C)
     */
    void begin(bool isTop, float targetTemp, float startTemp);

    /**
     * @brief Periodic step function called from the 100ms control task loop.
     * @param currentTemp Current measured temperature (°C)
     * @param[out] outputPower Output power to apply to heater (0.0f - 100.0f %)
     * @return true if autotuner is still running, false if finished or aborted
     */
    bool step(float currentTemp, float &outputPower);

    /**
     * @brief Abort autotuning immediately and safely set output to 0.
     */
    void abort();

    /**
     * @brief Apply computed tuning gains directly to a PIDController instance.
     * @param pid PIDController instance to update
     */
    void applyResults(PIDController &pid);

    /**
     * @brief Check whether autotuning is actively executing.
     * @return true if tuning in progress
     */
    bool isRunning() const { return _running; }

    /**
     * @brief Check whether autotuning has completed successfully.
     * @return true if tuning finished with valid gains
     */
    bool isFinished() const { return _finished; }

    /**
     * @brief Check whether active/last test is for the top heater.
     * @return true if Top heater, false if Bottom heater
     */
    bool isTop() const { return _isTop; }

    /**
     * @brief Get target temperature of active/last autotune test.
     * @return Target temperature in °C
     */
    float getTargetTemp() const { return _targetTemp; }

    /**
     * @brief Get estimated autotuning progress percentage (0.0% to 100.0%).
     * @return Progress in percent
     */
    float getProgressPercent() const;

    /**
     * @brief Retrieve computed PID gains from completed autotune test.
     * @param[out] kp Resulting proportional gain
     * @param[out] ki Resulting integral gain
     * @param[out] kd Resulting derivative gain
     */
    void getResults(float &kp, float &ki, float &kd) const {
        kp = _kp;
        ki = _ki;
        kd = _kd;
    }

private:
    bool     _isTop         = true;
    float    _targetTemp    = 150.0f;
    float    _eStopTemp     = 280.0f;
    float    _inputTemp     = 0.0f;
    float    _outputPower   = 0.0f;
    bool     _running       = false;
    bool     _finished      = false;
    float    _kp            = 0.0f;
    float    _ki            = 0.0f;
    float    _kd            = 0.0f;

    sTune    _tuner;
};

} // namespace pid
