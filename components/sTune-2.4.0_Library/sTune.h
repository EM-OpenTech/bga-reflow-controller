/**
 * @file sTune.h
 * @brief Open loop PID autotuner using Inflection Point test method for ESP-IDF v6.0.2 C++.
 *
 * Based on the sTune Library for Arduino (v2.4.0) by Dlloydev https://github.com/Dlloydev/sTune
 * Copyright (c) 2022 Dlloydev <dlloydev@testcor.ca>
 * Licensed under the MIT License.
 *
 * ESP-IDF native C++ port by BGA Reflow Controller Team.
 * Replaces Arduino micros/millis with esp_timer_get_time() and Serial with ESP_LOG / printf.
 */

#pragma once
#ifndef S_TUNE_H_
#define S_TUNE_H_

#include <cstdint>
#include <cmath>
#include <cstdio>
#include "sTan.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"

/**
 * @class sTune
 * @brief Open-loop Inflection Point Autotuner for thermal systems.
 */
class sTune {

  public:

    enum Action : uint8_t { directIP, direct5T, reverseIP, reverse5T };
    enum SerialMode : uint8_t { printOFF, printALL, printSUMMARY, printDEBUG };
    enum TunerStatus : uint8_t { sample, test, tunings, runPid, timerPid };
    enum TuningMethod : uint8_t {
      ZN_PID, DampedOsc_PID, NoOvershoot_PID, CohenCoon_PID, Mixed_PID,
      ZN_PI, DampedOsc_PI, NoOvershoot_PI, CohenCoon_PI, Mixed_PI
    };

    /**
     * @brief Default constructor.
     */
    sTune();

    /**
     * @brief Parameterized constructor linking input and output process variables.
     */
    sTune(float *input, float *output, TuningMethod tuningMethod, Action action, SerialMode serialMode);

    /**
     * @brief Default destructor.
     */
    ~sTune() = default;

    /**
     * @brief Configures autotuner parameters.
     * @param inputSpan Input temperature range (°C)
     * @param outputSpan Output power range (%)
     * @param outputStart Starting power (%)
     * @param outputStep Step power (%) applied during test
     * @param testTimeSec Maximum total test duration (s)
     * @param settleTimeSec Initial settling duration before step (s)
     * @param samples Total sample points to record
     */
    void Configure(float inputSpan, float outputSpan, float outputStart, float outputStep,
                   uint32_t testTimeSec, uint32_t settleTimeSec, uint16_t samples);

    /**
     * @brief Executes one autotuning cycle step.
     * @return TunerStatus status code
     */
    uint8_t Run();

    /**
     * @brief Resets autotuner internal history and state.
     */
    void Reset();

    void printTestRun();
    void printResults();
    void printTunings();
    void printPidTuner(uint8_t everyNth);
    void plotter(float input, float output, float setpoint, float outputScale = 1.0f, uint8_t everyNth = 1);
    float softPwm(uint8_t relayPin, float input, float output, float setpoint = 0.0f, uint32_t windowSizeMs = 1000, uint8_t debounceMs = 0);

    // Set functions
    void SetEmergencyStop(float e_Stop);
    void SetControllerAction(Action Action);
    void SetSerialMode(SerialMode SerialMode);
    void SetTuningMethod(TuningMethod TuningMethod);

    // Query functions
    float GetKp();                  ///< Proportional gain calculation
    float GetKi();                  ///< Integral gain calculation
    float GetKd();                  ///< Derivative gain calculation
    float GetTi() const;            ///< Integral time constant (seconds)
    float GetTd() const;            ///< Derivative time constant (seconds)
    float GetProcessGain() const;   ///< Process gain (Ku)
    float GetDeadTime() const;      ///< Process apparent dead time (seconds)
    float GetTau() const;           ///< Process time constant Tau (seconds)
    uint8_t GetControllerAction() const;
    uint8_t GetSerialMode() const;
    uint8_t GetTuningMethod() const;
    void GetAutoTunings(float * kp, float * ki, float * kd);
    uint16_t GetSampleCount() const { return sampleCount; }
    uint16_t GetTotalSamples() const { return _samples; }
    float GetProgress() const { return (_samples > 0) ? (static_cast<float>(sampleCount) / static_cast<float>(_samples) * 100.0f) : 0.0f; }

  private:

    sTan tangent;  // Instance member for multi-channel isolation (Top & Bottom heater support)

    Action _action = directIP;
    SerialMode _serialMode = printSUMMARY;
    TunerStatus _tunerStatus = test;
    TuningMethod _tuningMethod = ZN_PID;

    float *_input = nullptr;
    float *_output = nullptr;
    float _settlePeriodUs = 0.0f;
    float _samplePeriodUs = 0.0f;
    float _tangentPeriodUs = 0.0f;
    float _inputSpan = 100.0f;
    float _outputSpan = 255.0f;
    float _outputStart = 0.0f;
    float _outputStep = 0.0f;

    float eStop = 100.0f;
    float pvInst = 0.0f;
    float pvAvg = 0.0f;
    float pvIp = 0.0f;
    float pvMax = 0.0f;
    float pvPk = 0.0f;
    float pvInstRes = 0.0f;
    float pvAvgRes = 0.0f;
    float slopeIp = 0.0f;
    float pvTangent = 0.0f;
    float pvTangentPrev = 0.0f;
    float pvStart = 0.0f;

    float _kp = 0.0f;
    float _ki = 0.0f;
    float _kd = 0.0f;
    float _Ku = 0.0f;
    float _Tu = 0.0f;
    float _td = 0.0f;
    float _R = 0.0f;
    float _Ko = 0.0f;

    uint16_t _bufferSize = 0;
    uint16_t _samples = 0;
    uint16_t sampleCount = 0;
    uint16_t pvPkCount = 0;
    uint16_t ipCount = 0;
    uint16_t plotCount = 0;
    uint16_t eStopAbort = 0;

    uint32_t _settleTimeSec = 0;
    uint32_t _testTimeSec = 0;
    int64_t usPrev = 0;
    int64_t settlePrev = 0;
    int64_t usStart = 0;
    int64_t us = 0;
    int64_t ipUs = 0;

    const float kexp = 4.3004f; // (1 / exp(-1)) / (1 - exp(-1))
    const float epsilon = 0.0001f;
};

#endif // S_TUNE_H_
