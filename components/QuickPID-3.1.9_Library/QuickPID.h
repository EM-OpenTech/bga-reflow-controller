/**
 * @file QuickPID.h
 * @brief QuickPID Engine ported natively to ESP-IDF v6.0.2 C++.
 *
 * Based on the QuickPID Library for Arduino (v3.1.9) by David Lloyd https://github.com/Dlloydev/QuickPID
 * Copyright (c) 2023 David Lloyd <dlloydev@testcor.ca>
 * Licensed under the MIT License.
 *
 * ESP-IDF native C++ port by BGA Reflow Controller Team.
 * Replaces Arduino timing (micros) with ESP-IDF high-resolution timer (esp_timer_get_time).
 */

#pragma once
#ifndef QUICKPID_H
#define QUICKPID_H

#include <cstdint>
#include <algorithm>
#include <cmath>
#include "esp_timer.h"

/**
 * @class QuickPID
 * @brief Fast, feature-complete PID controller implementation.
 */
class QuickPID {

  public:

    enum class Control : uint8_t { manual, automatic, timer, toggle };  ///< Controller mode
    enum class Action : uint8_t { direct, reverse };                    ///< Controller action direction
    enum class pMode : uint8_t { pOnError, pOnMeas, pOnErrorMeas };     ///< Proportional mode
    enum class dMode : uint8_t { dOnError, dOnMeas };                   ///< Derivative mode
    enum class iAwMode : uint8_t { iAwCondition, iAwClamp, iAwOff };    ///< Integral anti-windup mode

    /**
     * @brief Default constructor.
     */
    QuickPID();

    /**
     * @brief Full constructor. Links PID to Input, Output, Setpoint, tuning parameters and control modes.
     */
    QuickPID(float *Input, float *Output, float *Setpoint, float Kp, float Ki, float Kd,
             pMode pMode, dMode dMode, iAwMode iAwMode, Action Action);

    /**
     * @brief Overload constructor with default proportional, derivative and anti-windup modes.
     */
    QuickPID(float *Input, float *Output, float *Setpoint, float Kp, float Ki, float Kd, Action Action);

    /**
     * @brief Simplified constructor with defaults for remaining parameters.
     */
    QuickPID(float *Input, float *Output, float *Setpoint);

    /**
     * @brief Sets PID mode to manual, automatic, timer or toggle.
     * @param Mode Controller mode
     */
    void SetMode(Control Mode);
    void SetMode(uint8_t Mode);

    /**
     * @brief Performs PID calculation.
     * @return true when a new output value is computed, false otherwise.
     */
    bool Compute();

    /**
     * @brief Sets and clamps output limits.
     * @param Min Lower clamp limit
     * @param Max Upper clamp limit
     */
    void SetOutputLimits(float Min, float Max);

    /**
     * @brief Sets tunings with current modes.
     * @param Kp Proportional gain
     * @param Ki Integral gain
     * @param Kd Derivative gain
     */
    void SetTunings(float Kp, float Ki, float Kd);

    /**
     * @brief Sets tunings and specifies proportional, derivative, and anti-windup modes.
     * @param Kp Proportional gain
     * @param Ki Integral gain
     * @param Kd Derivative gain
     * @param pMode Proportional mode
     * @param dMode Derivative mode
     * @param iAwMode Anti-windup mode
     */
    void SetTunings(float Kp, float Ki, float Kd, pMode pMode, dMode dMode, iAwMode iAwMode);

    /**
     * @brief Sets controller direction (direct or reverse).
     * @param Action Controller action
     */
    void SetControllerDirection(Action Action);
    void SetControllerDirection(uint8_t Direction);

    /**
     * @brief Sets sample time in microseconds (default is 100,000 µs / 100 ms).
     * @param NewSampleTimeUs Sample time in microseconds
     */
    void SetSampleTimeUs(uint32_t NewSampleTimeUs);

    /**
     * @brief Sets proportional mode.
     */
    void SetProportionalMode(pMode pMode);
    void SetProportionalMode(uint8_t Pmode);

    /**
     * @brief Sets derivative mode.
     */
    void SetDerivativeMode(dMode dMode);
    void SetDerivativeMode(uint8_t Dmode);

    /**
     * @brief Sets integral anti-windup mode.
     */
    void SetAntiWindupMode(iAwMode iAwMode);
    void SetAntiWindupMode(uint8_t IawMode);

    /**
     * @brief Sets internal output summation value.
     * @param sum Integral accumulator value
     */
    void SetOutputSum(float sum);

    /**
     * @brief Performs bumpless transfer from manual to automatic mode.
     */
    void Initialize();

    /**
     * @brief Clears pTerm, iTerm, dTerm, outputSum and timing history.
     */
    void Reset();

    // Query functions
    float GetKp() const;            ///< Returns configured proportional gain
    float GetKi() const;            ///< Returns configured integral gain
    float GetKd() const;            ///< Returns configured derivative gain
    float GetPterm() const;         ///< Returns proportional component of output
    float GetIterm() const;         ///< Returns integral component of output
    float GetDterm() const;         ///< Returns derivative component of output
    float GetOutputSum() const;     ///< Returns summation of all PID term components
    uint8_t GetMode() const;        ///< Returns current mode (manual: 0, automatic: 1, timer: 2, toggle: 3)
    uint8_t GetDirection() const;   ///< Returns direction (direct: 0, reverse: 1)
    uint8_t GetPmode() const;       ///< Returns pMode (pOnError: 0, pOnMeas: 1, pOnErrorMeas: 2)
    uint8_t GetDmode() const;       ///< Returns dMode (dOnError: 0, dOnMeas: 1)
    uint8_t GetAwMode() const;      ///< Returns iAwMode (iAwCondition: 0, iAwClamp: 1, iAwOff: 2)

    float outputSum = 0.0f;   ///< Internal integral sum

  private:

    float dispKp = 0.0f;      ///< Gains stored for display/query
    float dispKi = 0.0f;
    float dispKd = 0.0f;
    float pTerm = 0.0f;
    float iTerm = 0.0f;
    float dTerm = 0.0f;

    float kp = 0.0f;          ///< Internal scaled tuning gain P
    float ki = 0.0f;          ///< Internal scaled tuning gain I
    float kd = 0.0f;          ///< Internal scaled tuning gain D

    float *myInput = nullptr;     ///< Pointer to input variable
    float *myOutput = nullptr;    ///< Pointer to output variable
    float *mySetpoint = nullptr;  ///< Pointer to setpoint variable

    Control mode = Control::manual;
    Action action = Action::direct;
    pMode pmode = pMode::pOnError;
    dMode dmode = dMode::dOnMeas;
    iAwMode iawmode = iAwMode::iAwCondition;

    uint32_t sampleTimeUs = 100000; ///< Sample time in microseconds
    int64_t lastTime = 0;           ///< Microsecond timestamp from esp_timer_get_time()
    float outMin = 0.0f;
    float outMax = 255.0f;
    float error = 0.0f;
    float lastError = 0.0f;
    float lastInput = 0.0f;

}; // class QuickPID

#endif // QUICKPID_H
