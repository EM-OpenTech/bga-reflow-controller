/**
 * @file pid_controller.cpp
 * @brief Implementation of PID Controller wrapper for QuickPID.
 *
 * Configured specifically for thermal inertia systems (open-air ceramic
 * and quartz infrared heaters) with Proportional-on-Measurement,
 * Derivative-on-Measurement, and Anti-Windup Clamping.
 *
 * @author ESP-IDF Reflow Controller Team
 * @date 2026-09-23
 */

#include "pid/pid_controller.hpp"
#include <algorithm>

namespace pid {

PIDController::PIDController(float kp, float ki, float kd)
    : _input(0.0f),
      _output(0.0f),
      _setpoint(0.0f),
      _kp(kp),
      _ki(ki),
      _kd(kd),
      _automatic(false),
      _quickPid(&_input, &_output, &_setpoint, kp, ki, kd,
                QuickPID::pMode::pOnMeas,      // Proportional-on-Measurement (prevents setpoint kicks & overshoot)
                QuickPID::dMode::dOnMeas,      // Derivative-on-Measurement (prevents derivative spikes)
                QuickPID::iAwMode::iAwClamp,   // Integral Anti-Windup Clamping (prevents thermal lag accumulation)
                QuickPID::Action::direct) {}   // Direct action (heating increases temperature)

void PIDController::begin() {
    _quickPid.SetOutputLimits(DEFAULT_PID_OUTPUT_MIN, DEFAULT_PID_OUTPUT_MAX);
    _quickPid.SetSampleTimeUs(FIXED_PID_SAMPLE_TIME_US);
    _quickPid.SetMode(QuickPID::Control::manual);
    _automatic = false;
    _output = 0.0f;
}

void PIDController::setTunings(float kp, float ki, float kd) {
    _kp = kp;
    _ki = ki;
    _kd = kd;
    _quickPid.SetTunings(_kp, _ki, _kd);
}

void PIDController::setSetpoint(float setpoint) {
    _setpoint = setpoint;
}

void PIDController::setInput(float input) {
    _input = input;
}

bool PIDController::compute() {
    if (!_automatic) {
        return false;
    }
    return _quickPid.Compute();
}

void PIDController::reset() {
    _quickPid.Reset();
    _output = 0.0f;
    if (_automatic) {
        _quickPid.SetMode(QuickPID::Control::timer);
    }
}

void PIDController::setAutomatic(bool enabled) {
    _automatic = enabled;
    _quickPid.SetMode(enabled ? QuickPID::Control::timer : QuickPID::Control::manual);
}

void PIDController::setManualOutput(float percent) {
    _output = std::clamp(percent, DEFAULT_PID_OUTPUT_MIN, DEFAULT_PID_OUTPUT_MAX);
    if (!_automatic) {
        _quickPid.outputSum = _output; // Sync output sum for bumpless transfer when switching back to auto
    }
}

void PIDController::setOutputLimits(float minPercent, float maxPercent) {
    _quickPid.SetOutputLimits(minPercent, maxPercent);
}

} // namespace pid
