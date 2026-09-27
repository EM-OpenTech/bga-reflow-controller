/**
 * @file sTune.cpp
 * @brief Implementation of open-loop inflection point PID autotuner for ESP-IDF v6.0.2.
 *
 * Based on the sTune Library for Arduino (v2.4.0) by Dlloydev https://github.com/Dlloydev/sTune
 * Copyright (c) 2022 Dlloydev <dlloydev@testcor.ca>
 * Licensed under the MIT License.
 *
 * ESP-IDF native C++ port by BGA Reflow Controller Team.
 * Ported natively to ESP-IDF v6.0.2 C++.
 */

#include "sTune.h"

static const char *TAG = "sTune";

sTune::sTune() {
  _input = nullptr;
  _output = nullptr;
  sTune::Reset();
}

sTune::sTune(float *input, float *output, TuningMethod tuningMethod, Action action, SerialMode serialMode) {
  _input = input;
  _output = output;
  _tuningMethod = tuningMethod;
  _action = action;
  _serialMode = serialMode;
  sTune::Reset();
}

void sTune::Reset() {
  _tunerStatus = test;
  if (_output) *_output = _outputStart;
  usPrev = esp_timer_get_time();
  settlePrev = usPrev;
  usStart = 0;
  ipUs = 0;
  us = 0;
  _Ku = 0.0f;
  _Tu = 0.0f;
  _td = 0.0f;
  _kp = 0.0f;
  _ki = 0.0f;
  _kd = 0.0f;
  pvIp = 0.0f;
  pvMax = 0.0f;
  pvPk = 0.0f;
  slopeIp = 0.0f;
  pvTangent = 0.0f;
  pvTangentPrev = 0.0f;
  pvInst = 0.0f;
  pvAvg = 0.0f;
  pvStart = 0.0f;
  pvInstRes = 0.0f;
  pvAvgRes = 0.0f;
  ipCount = 0;
  plotCount = 0;
  sampleCount = 0;
  pvPkCount = 0;
  eStopAbort = 0;
}

void sTune::Configure(float inputSpan, float outputSpan, float outputStart, float outputStep,
                      uint32_t testTimeSec, uint32_t settleTimeSec, uint16_t samples) {
  _inputSpan = inputSpan;
  eStop = inputSpan;
  _outputSpan = outputSpan;
  _outputStart = outputStart;
  _outputStep = outputStep;
  _testTimeSec = testTimeSec;
  _settleTimeSec = settleTimeSec;
  _samples = samples;
  _bufferSize = static_cast<uint16_t>(_samples * 0.06f);
  if (_bufferSize < 2) _bufferSize = 2;
  _samplePeriodUs = (static_cast<float>(_testTimeSec) * 1000000.0f) / static_cast<float>(_samples);
  _tangentPeriodUs = _samplePeriodUs * static_cast<float>(_bufferSize - 1);
  _settlePeriodUs = static_cast<float>(_settleTimeSec) * 1000000.0f;

  tangent.begin(_bufferSize);
  sTune::Reset();
}

uint8_t sTune::Run() {
  if (!_input || !_output) return timerPid;

  int64_t usNow = esp_timer_get_time();
  int64_t usElapsed = usNow - usPrev;
  int64_t settleElapsed = usNow - settlePrev;
  us = usNow - usStart;

  switch (_tunerStatus) {

    case sample:
      _tunerStatus = test;
      return test;

    case test: // run inflection point test method
      if (pvInst > eStop && !eStopAbort) {
        sTune::Reset();
        sampleCount = _samples + 1;
        eStopAbort = 1;
        ESP_LOGE(TAG, "ABORT: pvInst (%f) > eStop (%f)", pvInst, eStop);
        break;
      }

      if (static_cast<float>(settleElapsed) >= _settlePeriodUs) { // settling period expired
        if (sampleCount == 1) *_output = _outputStep;

        if (static_cast<float>(usElapsed) >= _samplePeriodUs) { // ready to process sample
          usPrev = usNow;

          if (sampleCount <= _samples) { // continue testing

            float lastPvInst = pvInst;
            float lastPvAvg = pvAvg;
            pvInst = *_input;
            pvAvg = tangent.avgVal(pvInst);

            float pvInstResolution = std::fabs(pvInst - lastPvInst);
            float pvAvgResolution = std::fabs(pvAvg - lastPvAvg);

            if (pvInstResolution > epsilon && (pvInstRes == 0.0f || pvInstResolution < pvInstRes)) pvInstRes = pvInstResolution;
            if (pvAvgResolution > epsilon && (pvAvgRes == 0.0f || pvAvgResolution < pvAvgRes)) pvAvgRes = pvAvgResolution;

            if (sampleCount == 0) { // initialize at first sample
              tangent.init(pvInst);
              pvAvg = pvInst;
              pvInstResolution = 0.0f;
              pvAvgResolution = 0.0f;
              pvInstRes = pvInst;
              pvAvgRes = pvInst;
              pvStart = pvInst;
              usStart = usNow;
              us = 0;
            }

            // Sliding tangent line calculation
            pvTangent = pvAvg - tangent.startVal();

            // Check for dead time
            bool dt = false;
            if (_action == directIP || _action == direct5T) {
              dt = (pvAvg > pvStart + pvInstRes + epsilon);
            } else { // reverse
              dt = (pvAvg < pvStart - pvInstRes - epsilon);
            }
            if (_td == 0.0f && dt) {
              _td = static_cast<float>(us) * 0.000001f;
            }

            // Check for inflection point
            bool ipcount = false;
            if (_action == directIP || _action == direct5T) {
              if (pvTangent > slopeIp + epsilon) ipcount = true;
              if (pvTangent < 0.0f + epsilon) ipCount = 0; // flat or negative tangent
            } else { // reverse
              if (pvTangent < slopeIp - epsilon) ipcount = true;
              if (pvTangent > 0.0f - epsilon) ipCount = 0; // flat or positive tangent
            }

            if (ipcount) {
              ipCount = 0;
              slopeIp = pvTangent;
            }
            ipCount++;

            if ((_action == directIP || _action == reverseIP) && (ipCount == static_cast<uint16_t>(_samples / 16))) {
              sampleCount = _samples;
              ipUs = us;
              pvIp = pvAvg;

              // apparent pvMax
              pvMax = pvIp + (slopeIp * kexp);

              // apparent tangent from pvStart to pvMax crossing points
              _Tu = (((pvMax - pvStart) / slopeIp) * _tangentPeriodUs * 0.000001f) - _td;
            }

            if (_action == direct5T || _action == reverse5T) {
              if (sampleCount >= _samples - 1) sampleCount = _samples - 2;
              if (us > static_cast<int64_t>(_testTimeSec) * 100000LL) { // 10% elapsed
                if (pvAvg > pvPk) {
                  pvPk = pvAvg + (static_cast<float>(_bufferSize) * 0.2f * pvAvgRes);
                  pvPkCount = 0;
                } else {
                  pvPkCount++;
                }

                if (pvPkCount == static_cast<uint16_t>(1.2f * static_cast<float>(_bufferSize))) {
                  pvPkCount++;
                  sampleCount = _samples;
                  pvMax = pvAvg + (pvInst - pvStart) * 0.05f;
                  _Tu = (static_cast<float>(us) * 1.6667f * 0.000001f * 0.286f) - _td;
                }
              }
            }

            if (sampleCount == _samples) { // testing complete
              _R = _td / (_Tu + epsilon);

              // process gain
              float deltaInputSpan = _inputSpan > 0.0f ? _inputSpan : 1.0f;
              float deltaOutputSpan = _outputSpan > 0.0f ? _outputSpan : 1.0f;
              float deltaOutStep = (_outputStep - _outputStart) / deltaOutputSpan;
              if (std::fabs(deltaOutStep) < epsilon) deltaOutStep = 1.0f;

              _Ku = std::fabs(((pvMax - pvStart) / deltaInputSpan) / deltaOutStep);

              _kp = sTune::GetKp();
              _ki = sTune::GetKi();
              _kd = sTune::GetKd();

              sTune::printResults();
              _tunerStatus = tunings;
              return tunings;
            }

            sTune::printTestRun();
            pvTangentPrev = pvTangent;
          } else {
            _tunerStatus = tunings;
          }
          sampleCount++;
          _tunerStatus = sample;
          return sample;
        }

      } else { // settling
        if (static_cast<float>(usElapsed) >= _samplePeriodUs && !eStopAbort) {
          *_output = _outputStart;
          usPrev = usNow;
          pvInst = *_input;
          if (_serialMode == printALL || _serialMode == printDEBUG) {
            float remainSec = static_cast<float>(_settlePeriodUs - static_cast<float>(settleElapsed)) * 0.000001f;
            ESP_LOGI(TAG, "sec: %.4f | out: %.2f | pv: %.3f | settling...", remainSec, *_output, pvInst);
          }
          _tunerStatus = sample;
          return sample;
        }
      }
      break;

    case tunings:
      _tunerStatus = timerPid;
      return timerPid;

    case runPid:
      if (pvInst > eStop && !eStopAbort) {
        sTune::Reset();
        sampleCount = _samples + 1;
        eStopAbort = 1;
        ESP_LOGE(TAG, "ABORT: pvInst > eStop");
      }
      _tunerStatus = timerPid;
      return timerPid;

    case timerPid:
      if (static_cast<float>(usElapsed) >= _samplePeriodUs) {
        usPrev = usNow;
        _tunerStatus = runPid;
        return runPid;
      } else {
        _tunerStatus = timerPid;
        return timerPid;
      }

    default:
      _tunerStatus = timerPid;
      return timerPid;
  }
  return timerPid;
}

void sTune::SetEmergencyStop(float e_Stop) { eStop = e_Stop; }
void sTune::SetControllerAction(Action ActionVal) { _action = ActionVal; }
void sTune::SetSerialMode(SerialMode SerialModeVal) { _serialMode = SerialModeVal; }
void sTune::SetTuningMethod(TuningMethod TuningMethodVal) { _tuningMethod = TuningMethodVal; }

void sTune::printPidTuner(uint8_t everyNth) {
  if (sampleCount < _samples) {
    if (plotCount == 0 || plotCount >= everyNth) {
      plotCount = 1;
      printf("%.4f, %.2f, %.3f\n", static_cast<float>(us) * 0.000001f, _output ? *_output : 0.0f, pvAvg);
    } else plotCount++;
  }
}

void sTune::plotter(float input, float output, float setpoint, float outputScale, uint8_t everyNth) {
  if (plotCount >= everyNth) {
    plotCount = 1;
    printf("Setpoint:%.2f, Input:%.2f, Output:%.2f\n", setpoint, input, output * outputScale);
  } else plotCount++;
}

void sTune::printTestRun() {
  if (sampleCount < _samples) {
    if (_serialMode == printALL || _serialMode == printDEBUG) {
      float sec = static_cast<float>(us) * 0.000001f;
      float outVal = _output ? *_output : 0.0f;
      ESP_LOGI(TAG, "sec: %.4f | out: %.2f | pv: %.3f | tan: %.3f", sec, outVal, pvInst, pvTangent);
    }
  }
}

void sTune::printTunings() {
  const char* methodStr = "Mixed_PI";
  switch (_tuningMethod) {
    case ZN_PID: methodStr = "ZN_PID"; break;
    case DampedOsc_PID: methodStr = "Damped_PID"; break;
    case NoOvershoot_PID: methodStr = "NoOvershoot_PID"; break;
    case CohenCoon_PID: methodStr = "CohenCoon_PID"; break;
    case Mixed_PID: methodStr = "Mixed_PID"; break;
    case ZN_PI: methodStr = "ZN_PI"; break;
    case DampedOsc_PI: methodStr = "Damped_PI"; break;
    case NoOvershoot_PI: methodStr = "NoOvershoot_PI"; break;
    case CohenCoon_PI: methodStr = "CohenCoon_PI"; break;
    default: methodStr = "Mixed_PI"; break;
  }

  ESP_LOGI(TAG, "Tuning Method: %s", methodStr);
  ESP_LOGI(TAG, "  Kp: %.3f", sTune::GetKp());
  ESP_LOGI(TAG, "  Ki: %.3f | Ti: %.3f", sTune::GetKi(), sTune::GetTi());
  ESP_LOGI(TAG, "  Kd: %.3f | Td: %.3f", sTune::GetKd(), sTune::GetTd());
}

void sTune::printResults() {
  if (_serialMode == printALL || _serialMode == printDEBUG || _serialMode == printSUMMARY) {
    ESP_LOGI(TAG, "------------------- sTune Results -------------------");
    ESP_LOGI(TAG, "Action: %d | Start: %.2f | Step: %.2f | SampleSec: %.4f",
             static_cast<int>(_action), _outputStart, _outputStep, _samplePeriodUs * 0.000001f);
    ESP_LOGI(TAG, "PvStart: %.3f | PvMax/Min: %.3f | PvDiff: %.3f",
             pvStart, pvMax, pvMax - pvStart);
    ESP_LOGI(TAG, "Process Gain (Ku): %.3f | DeadTime (td): %.3f s | Tau: %.3f s",
             _Ku, _td, _Tu);

    float controllability = _Tu / (_td + epsilon);
    if (controllability > 99.9f) controllability = 99.9f;
    const char* cEval = (controllability > 0.75f) ? "easy" : (controllability > 0.25f ? "average" : "difficult");
    ESP_LOGI(TAG, "Tau/DeadTime ratio: %.1f (%s controllability)", controllability, cEval);

    sTune::printTunings();
    ESP_LOGI(TAG, "-----------------------------------------------------");
  }
}

void sTune::GetAutoTunings(float * kp, float * ki, float * kd) {
  if (kp) *kp = _kp;
  if (ki) *ki = _ki;
  if (kd) *kd = _kd;
}

float sTune::GetKp() {
  float znPid = ((1.2f * _Tu) / (_Ku * (_td + epsilon))) / 2.0f;
  float doPid = (0.66f * _Tu) / (_Ku * (_td + epsilon));
  float noPid = (0.6f / (_Ku + epsilon)) * (_Tu / (_td + epsilon));
  float ccPid = _Ku * (1.33f + (_R / 4.0f));
  float znPi = ((0.9f * _Tu) / (_Ku * (_td + epsilon))) / 2.0f;
  float doPi = (0.495f * _Tu) / (_Ku * (_td + epsilon));
  float noPi = (0.35f / (_Ku + epsilon)) * (_Tu / (_td + epsilon));
  float ccPi = _Ku * (0.9f + (_R / 12.0f));

  switch (_tuningMethod) {
    case ZN_PID: _kp = znPid; break;
    case DampedOsc_PID: _kp = doPid; break;
    case NoOvershoot_PID: _kp = noPid; break;
    case CohenCoon_PID: _kp = ccPid; break;
    case Mixed_PID: _kp = 0.25f * (znPid + doPid + noPid + ccPid); break;
    case ZN_PI: _kp = znPi; break;
    case DampedOsc_PI: _kp = doPi; break;
    case NoOvershoot_PI: _kp = noPi; break;
    case CohenCoon_PI: _kp = ccPi; break;
    default: _kp = 0.25f * (znPi + doPi + noPi + ccPi); break;
  }
  return _kp;
}

float sTune::GetKi() {
  float znPid = 1.0f / (2.0f * (_td + epsilon));
  float doPid = 1.0f / (_Tu / 3.6f + epsilon);
  float noPid = 1.0f / (_Tu + epsilon);
  float ccPid = 1.0f / ((_td * (30.0f + (3.0f * _R)) / (9.0f + (20.0f * _R))) + epsilon);
  float znPi = 1.0f / (3.3333f * (_td + epsilon));
  float doPi = 1.0f / (_Tu / 2.6f + epsilon);
  float noPi = 1.0f / (1.2f * _Tu + epsilon);
  float ccPi = 1.0f / ((_td * (30.0f + (3.0f * _R)) / (9.0f + (20.0f * _R))) + epsilon);

  switch (_tuningMethod) {
    case ZN_PID: _ki = znPid; break;
    case DampedOsc_PID: _ki = doPid; break;
    case NoOvershoot_PID: _ki = noPid; break;
    case CohenCoon_PID: _ki = ccPid; break;
    case Mixed_PID: _ki = 0.25f * (znPid + doPid + noPid + ccPid); break;
    case ZN_PI: _ki = znPi; break;
    case DampedOsc_PI: _ki = doPi; break;
    case NoOvershoot_PI: _ki = noPi; break;
    case CohenCoon_PI: _ki = ccPi; break;
    default: _ki = 0.25f * (znPi + doPi + noPi + ccPi); break;
  }
  return _ki;
}

float sTune::GetKd() {
  float znPid = 1.0f / (0.5f * (_td + epsilon));
  float doPid = 1.0f / (_Tu / 9.0f + epsilon);
  float noPid = 1.0f / (0.5f * (_td + epsilon));
  float ccPid = 1.0f / (((4.0f * _td) / (11.0f + (2.0f * _R))) + epsilon);

  switch (_tuningMethod) {
    case ZN_PID: _kd = znPid; break;
    case DampedOsc_PID: _kd = doPid; break;
    case NoOvershoot_PID: _kd = noPid; break;
    case CohenCoon_PID: _kd = ccPid; break;
    case Mixed_PID: _kd = 0.25f * (znPid + doPid + noPid + ccPid); break;
    default: _kd = 0.0f; break; // PI controller
  }
  return _kd;
}

float sTune::GetTi() const { return (_ki > 0.0f) ? (_kp / _ki) : 0.0f; }
float sTune::GetTd() const { return (_kd > 0.0f) ? (_kp / _kd) : 0.0f; }
float sTune::GetProcessGain() const { return _Ku; }
float sTune::GetDeadTime() const { return _td; }
float sTune::GetTau() const { return _Tu; }
uint8_t sTune::GetControllerAction() const { return static_cast<uint8_t>(_action); }
uint8_t sTune::GetSerialMode() const { return static_cast<uint8_t>(_serialMode); }
uint8_t sTune::GetTuningMethod() const { return static_cast<uint8_t>(_tuningMethod); }

float sTune::softPwm(uint8_t relayPin, float input, float output, float setpoint, uint32_t windowSizeMs, uint8_t debounceMs) {
  int64_t msNow = esp_timer_get_time() / 1000;
  static int64_t windowStartTime = 0;
  static int64_t nextSwitchTime = 0;

  if (msNow - windowStartTime >= windowSizeMs) {
    windowStartTime = msNow;
  }

  static float optimumOutput = 0.0f;
  static bool reachedSetpoint = false;

  if (input > setpoint) reachedSetpoint = true;
  if (reachedSetpoint && debounceMs == 0 && setpoint > 0.0f && input > setpoint) optimumOutput = output - 8.0f;
  else if (reachedSetpoint && debounceMs == 0 && setpoint > 0.0f && input < setpoint) optimumOutput = output + 8.0f;
  else optimumOutput = output;
  if (optimumOutput < 0.0f) optimumOutput = 0.0f;

  static bool relayStatus = false;
  if (!relayStatus && optimumOutput > static_cast<float>(msNow - windowStartTime)) {
    if (msNow > nextSwitchTime) {
      nextSwitchTime = msNow + debounceMs;
      relayStatus = true;
      gpio_set_level(static_cast<gpio_num_t>(relayPin), 1);
    }
  } else if (relayStatus && optimumOutput < static_cast<float>(msNow - windowStartTime)) {
    if (msNow > nextSwitchTime) {
      nextSwitchTime = msNow + debounceMs;
      relayStatus = false;
      gpio_set_level(static_cast<gpio_num_t>(relayPin), 0);
    }
  }
  return optimumOutput;
}
