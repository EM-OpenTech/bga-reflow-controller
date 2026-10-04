/*
 * SPDX-FileCopyrightText: 2022 Dlloydev <dlloydev@testcor.ca>
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: MIT AND AGPL-3.0-or-later
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
 * @file sTune.hpp
 * @brief sTune Autotuner Library (v2.4.0) ported to ESP-IDF C++.
 *
 * Original:
 *   sTune Library for Arduino - Version 2.4.0
 *   by dlloydev https://github.com/Dlloydev/sTune
 *   Licensed under the MIT License.
 *   This is an open loop PID autotuner using a novel s-curve inflection point test method.
 *   Tuning parameters are determined in about 1/2Tau on a first-order system with time delay.
 *   Full 5Tau testing and multiple serial output options are provided.
 *
 * ESP-IDF C++ Port:
 *   Ported by EM-OpenTech for BGA Reflow Controller.
 *   Replaces Arduino timing (micros/millis) with esp_timer_get_time() and Serial with ESP_LOG.
 *
 * @copyright Copyright (c) 2022 Dlloydev <dlloydev@testcor.ca>, MIT License
 * @copyright Copyright (c) 2026 EM-OpenTech (ESP-IDF Port), AGPL-3.0-or-later
 * @see https://github.com/Dlloydev/sTune
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once
#ifndef S_TUNE_HPP_
#define S_TUNE_HPP_

#include <cstdint>
#include <cmath>
#include <cstdio>
#include "sTan.hpp"
#include "esp_timer.h"
#include "esp_log.h"

/**
 * @class sTune
 * @brief Open-loop Inflection Point Autotuner for thermal systems.
 */
class sTune {

  public:

    /// Autotuner controller action & test algorithm
    enum Action : uint8_t {
        directIP  = 0, ///< Direct acting heating, Inflection Point method
        direct5T  = 1, ///< Direct acting heating, 5-Tau full test method
        reverseIP = 2, ///< Reverse acting cooling, Inflection Point method
        reverse5T = 3  ///< Reverse acting cooling, 5-Tau full test method
    };

    /// Serial / Logging verbosity levels
    enum SerialMode : uint8_t {
        printOFF     = 0, ///< No log outputs
        printALL     = 1, ///< Verbose log output per sample
        printSUMMARY = 2, ///< Print summary report upon completion
        printDEBUG   = 3  ///< Detailed debug telemetry logging
    };

    /// Current execution state of the autotuner
    enum TunerStatus : uint8_t {
        sample   = 0, ///< Baseline settling & sampling phase
        test     = 1, ///< Step input response test active
        tunings  = 2, ///< Calculations complete, tunings ready
        runPid   = 3, ///< Tuning applied to PID controller
        timerPid = 4  ///< Timer-based PID execution
    };

    /// PID tuning rule calculation methods
    enum TuningMethod : uint8_t {
        ZN_PID,          ///< Ziegler-Nichols PID rule (classic fast response)
        DampedOsc_PID,   ///< Damped Oscillation PID rule (reduced overshoot)
        NoOvershoot_PID, ///< No-Overshoot PID rule (conservative thermal profile)
        CohenCoon_PID,   ///< Cohen-Coon PID rule (optimized for systems with dead-time)
        Mixed_PID,       ///< Averaged / blended PID rule
        ZN_PI,           ///< Ziegler-Nichols PI rule
        DampedOsc_PI,    ///< Damped Oscillation PI rule
        NoOvershoot_PI,  ///< No-Overshoot PI rule
        CohenCoon_PI,    ///< Cohen-Coon PI rule
        Mixed_PI         ///< Averaged / blended PI rule
    };

    /**
     * @brief Default constructor.
     */
    sTune();

    /**
     * @brief Parameterized constructor linking input and output process variables.
     * @param input Pointer to measured process variable (temperature °C)
     * @param output Pointer to manipulated output variable (power %)
     * @param tuningMethod Selected tuning algorithm rule
     * @param action Test method and controller action direction
     * @param serialMode Log output verbosity level
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
     * @return TunerStatus status code (sample: 0, test: 1, tunings: 2, runPid: 3)
     */
    uint8_t Run();

    /**
     * @brief Resets autotuner internal history and state.
     */
    void Reset();

    /**
     * @brief Prints test run configuration header to console.
     */
    void printTestRun();

    /**
     * @brief Prints calculated process parameters (Ku, Tu, td, R) to console.
     */
    void printResults();

    /**
     * @brief Prints calculated Kp, Ki, Kd gains for selected tuning method.
     */
    void printTunings();

    /**
     * @brief Prints periodic PID telemetry row.
     * @param everyNth Decimation factor for print output
     */
    void printPidTuner(uint8_t everyNth);

    /**
     * @brief Serial plotter output helper for live graphing.
     * @param input Current measured temperature
     * @param output Current output power
     * @param setpoint Target setpoint temperature
     * @param outputScale Output scaling multiplier
     * @param everyNth Decimation factor
     */
    void plotter(float input, float output, float setpoint, float outputScale = 1.0f, uint8_t everyNth = 1);

    /**
     * @brief Software time-proportional PWM simulation helper.
     * @param relayPin Target GPIO relay pin number
     * @param input Process temperature
     * @param output Calculated power percentage
     * @param setpoint Setpoint temperature
     * @param windowSizeMs Time-proportioning cycle window in ms
     * @param debounceMs Switch debounce window in ms
     * @return PWM duty cycle value
     */
    float softPwm(uint8_t relayPin, float input, float output, float setpoint = 0.0f, uint32_t windowSizeMs = 1000, uint8_t debounceMs = 0);

    // Setters
    /**
     * @brief Sets emergency stop temperature threshold.
     * @param e_Stop Emergency stop cutoff temperature (°C)
     */
    void SetEmergencyStop(float e_Stop);

    /**
     * @brief Sets controller action and test method.
     * @param Action Action enum (directIP, direct5T, reverseIP, reverse5T)
     */
    void SetControllerAction(Action Action);

    /**
     * @brief Sets log output mode.
     * @param SerialMode Logging verbosity level enum
     */
    void SetSerialMode(SerialMode SerialMode);

    /**
     * @brief Sets tuning calculation method.
     * @param TuningMethod Tuning rule enum (e.g. CohenCoon_PID, ZN_PID)
     */
    void SetTuningMethod(TuningMethod TuningMethod);

    // Query functions
    float GetKp();                  ///< Calculated proportional gain Kp
    float GetKi();                  ///< Calculated integral gain Ki
    float GetKd();                  ///< Calculated derivative gain Kd
    float GetTi() const;            ///< Calculated integral time constant Ti (seconds)
    float GetTd() const;            ///< Calculated derivative time constant Td (seconds)
    float GetProcessGain() const;   ///< Calculated process gain Ku
    float GetDeadTime() const;      ///< Calculated apparent dead time L / td (seconds)
    float GetTau() const;           ///< Calculated process time constant Tau (seconds)
    uint8_t GetControllerAction() const; ///< Returns configured action enum value
    uint8_t GetSerialMode() const;       ///< Returns configured serial mode enum value
    uint8_t GetTuningMethod() const;     ///< Returns configured tuning method enum value

    /**
     * @brief Copies calculated PID gains into target pointers.
     * @param kp Output pointer for Kp gain
     * @param ki Output pointer for Ki gain
     * @param kd Output pointer for Kd gain
     */
    void GetAutoTunings(float * kp, float * ki, float * kd);

    uint16_t GetSampleCount() const { return sampleCount; }   ///< Number of recorded samples
    uint16_t GetTotalSamples() const { return _samples; }      ///< Total planned samples
    float GetProgress() const { return (_samples > 0) ? (static_cast<float>(sampleCount) / static_cast<float>(_samples) * 100.0f) : 0.0f; } ///< Autotuning progress percentage (0..100%)

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

#endif // S_TUNE_HPP_
