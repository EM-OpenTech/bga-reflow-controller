/*
 * SPDX-FileCopyrightText: 2023 David Lloyd <dlloydev@testcor.ca>
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
 * @file QuickPID.cpp
 * @brief Implementation of QuickPID Engine ported to ESP-IDF C++.
 *
 * Based on the QuickPID Library for Arduino (v3.1.9) by David Lloyd https://github.com/Dlloydev/QuickPID
 * All Arduino dependencies replaced with native ESP-IDF timing and standard C++.
 *
 * @copyright Copyright (c) 2023 David Lloyd <dlloydev@testcor.ca>, MIT License
 * @copyright Copyright (c) 2026 EM-OpenTech (ESP-IDF Port), AGPL-3.0-or-later
 * @see https://github.com/Dlloydev/QuickPID
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "QuickPID.hpp"

QuickPID::QuickPID() {}

/* Constructor ********************************************************************/
QuickPID::QuickPID(float* Input, float* Output, float* Setpoint,
                   float Kp, float Ki, float Kd,
                   pMode pModeVal, dMode dModeVal, iAwMode iAwModeVal, Action ActionVal) {
  myOutput = Output;
  myInput = Input;
  mySetpoint = Setpoint;
  mode = Control::manual;

  QuickPID::SetOutputLimits(0.0f, 100.0f);  // Default output limits (0–100%)
  sampleTimeUs = 100000;                    // 0.1s (100,000 µs) default sample time
  QuickPID::SetControllerDirection(ActionVal);
  QuickPID::SetTunings(Kp, Ki, Kd, pModeVal, dModeVal, iAwModeVal);

  lastTime = esp_timer_get_time() - sampleTimeUs;
}

/* Overload Constructor ***********************************************************/
QuickPID::QuickPID(float* Input, float* Output, float* Setpoint,
                   float Kp, float Ki, float Kd, Action ActionVal)
  : QuickPID(Input, Output, Setpoint, Kp, Ki, Kd,
             pMode::pOnError, dMode::dOnMeas, iAwMode::iAwCondition, ActionVal) {
}

/* Simplified Constructor *********************************************************/
QuickPID::QuickPID(float* Input, float* Output, float* Setpoint)
  : QuickPID(Input, Output, Setpoint, 0.0f, 0.0f, 0.0f, Action::direct) {
}

/* Compute() ***********************************************************************
 * Executes PID calculation using microsecond resolution timing via esp_timer_get_time().
 * Returns true when a new output value is computed, false otherwise.
 **********************************************************************************/
bool QuickPID::Compute() {
  if (mode == Control::manual) return false;

  int64_t now = esp_timer_get_time();
  int64_t timeChange = now - lastTime;

  if (mode == Control::timer || timeChange >= sampleTimeUs) {
    if (!myInput || !myOutput || !mySetpoint) return false;

    float input = *myInput;
    float dInput = input - lastInput;
    if (action == Action::reverse) dInput = -dInput;

    error = *mySetpoint - input;
    if (action == Action::reverse) error = -error;
    float dError = error - lastError;

    float peTerm = kp * error;
    float pmTerm = kp * dInput;
    if (pmode == pMode::pOnError) pmTerm = 0.0f;
    else if (pmode == pMode::pOnMeas) peTerm = 0.0f;
    else { // pOnErrorMeas
      peTerm *= 0.5f;
      pmTerm *= 0.5f;
    }

    pTerm = peTerm - pmTerm;
    iTerm = ki * error;

    if (dmode == dMode::dOnError) dTerm = kd * dError;
    else dTerm = -kd * dInput; // dOnMeas

    // Condition integral anti-windup (default)
    if (iawmode == iAwMode::iAwCondition) {
      bool aw = false;
      float iTermOut = (peTerm - pmTerm) + ki * (iTerm + error);
      if (iTermOut > outMax && dError > 0.0f) aw = true;
      else if (iTermOut < outMin && dError < 0.0f) aw = true;
      if (aw && ki != 0.0f) iTerm = std::clamp(iTermOut, -outMax, outMax);
    }

    // Compute PID sum and clamp output
    outputSum += iTerm;
    if (iawmode == iAwMode::iAwOff) {
      outputSum -= pmTerm;
    } else {
      outputSum = std::clamp(outputSum - pmTerm, outMin, outMax);
    }

    *myOutput = std::clamp(outputSum + peTerm + dTerm, outMin, outMax);

    lastError = error;
    lastInput = input;
    lastTime = now;
    return true;
  }
  return false;
}

/* SetTunings *********************************************************************/
void QuickPID::SetTunings(float Kp, float Ki, float Kd,
                          pMode pModeVal, dMode dModeVal, iAwMode iAwModeVal) {
  if (Kp < 0.0f || Ki < 0.0f || Kd < 0.0f) return;
  if (Ki == 0.0f) outputSum = 0.0f;

  pmode = pModeVal;
  dmode = dModeVal;
  iawmode = iAwModeVal;

  dispKp = Kp;
  dispKi = Ki;
  dispKd = Kd;

  float sampleTimeSec = static_cast<float>(sampleTimeUs) / 1000000.0f;
  kp = Kp;
  ki = Ki * sampleTimeSec;
  kd = Kd / sampleTimeSec;
}

void QuickPID::SetTunings(float Kp, float Ki, float Kd) {
  SetTunings(Kp, Ki, Kd, pmode, dmode, iawmode);
}

/* SetSampleTimeUs ****************************************************************/
void QuickPID::SetSampleTimeUs(uint32_t NewSampleTimeUs) {
  if (NewSampleTimeUs > 0) {
    float ratio = static_cast<float>(NewSampleTimeUs) / static_cast<float>(sampleTimeUs);
    ki *= ratio;
    kd /= ratio;
    sampleTimeUs = NewSampleTimeUs;
  }
}

/* SetOutputLimits ****************************************************************/
void QuickPID::SetOutputLimits(float Min, float Max) {
  if (Min >= Max) return;
  outMin = Min;
  outMax = Max;

  if (mode != Control::manual && myOutput != nullptr) {
    *myOutput = std::clamp(*myOutput, outMin, outMax);
    outputSum = std::clamp(outputSum, outMin, outMax);
  }
}

/* SetMode ************************************************************************/
void QuickPID::SetMode(Control Mode) {
  if (mode == Control::manual && Mode != Control::manual) {
    QuickPID::Initialize();
  }
  if (Mode == Control::toggle) {
    mode = (mode == Control::manual) ? Control::automatic : Control::manual;
  } else {
    mode = Mode;
  }
}

void QuickPID::SetMode(uint8_t Mode) {
  if (mode == Control::manual && Mode != 0) {
    QuickPID::Initialize();
  }
  if (Mode == 3) { // toggle
    mode = (mode == Control::manual) ? Control::automatic : Control::manual;
  } else {
    mode = static_cast<Control>(Mode);
  }
}

/* Initialize *********************************************************************/
void QuickPID::Initialize() {
  if (myOutput && myInput) {
    outputSum = *myOutput;
    lastInput = *myInput;
    outputSum = std::clamp(outputSum, outMin, outMax);
  }
}

/* SetControllerDirection *********************************************************/
void QuickPID::SetControllerDirection(Action ActionVal) {
  action = ActionVal;
}

void QuickPID::SetControllerDirection(uint8_t Direction) {
  action = static_cast<Action>(Direction);
}

/* SetProportionalMode ************************************************************/
void QuickPID::SetProportionalMode(pMode pModeVal) {
  pmode = pModeVal;
}

void QuickPID::SetProportionalMode(uint8_t Pmode) {
  pmode = static_cast<pMode>(Pmode);
}

/* SetDerivativeMode **************************************************************/
void QuickPID::SetDerivativeMode(dMode dModeVal) {
  dmode = dModeVal;
}

void QuickPID::SetDerivativeMode(uint8_t Dmode) {
  dmode = static_cast<dMode>(Dmode);
}

/* SetAntiWindupMode **************************************************************/
void QuickPID::SetAntiWindupMode(iAwMode iAwModeVal) {
  iawmode = iAwModeVal;
}

void QuickPID::SetAntiWindupMode(uint8_t IawMode) {
  iawmode = static_cast<iAwMode>(IawMode);
}

/* Reset **************************************************************************/
void QuickPID::Reset() {
  lastTime = esp_timer_get_time() - sampleTimeUs;
  lastInput = 0.0f;
  outputSum = 0.0f;
  pTerm = 0.0f;
  iTerm = 0.0f;
  dTerm = 0.0f;
}

/* SetOutputSum *******************************************************************/
void QuickPID::SetOutputSum(float sum) {
  outputSum = sum;
}

/* Query Functions ****************************************************************/
float QuickPID::GetKp() const { return dispKp; }
float QuickPID::GetKi() const { return dispKi; }
float QuickPID::GetKd() const { return dispKd; }
float QuickPID::GetPterm() const { return pTerm; }
float QuickPID::GetIterm() const { return iTerm; }
float QuickPID::GetDterm() const { return dTerm; }
float QuickPID::GetOutputSum() const { return outputSum; }
uint8_t QuickPID::GetMode() const { return static_cast<uint8_t>(mode); }
uint8_t QuickPID::GetDirection() const { return static_cast<uint8_t>(action); }
uint8_t QuickPID::GetPmode() const { return static_cast<uint8_t>(pmode); }
uint8_t QuickPID::GetDmode() const { return static_cast<uint8_t>(dmode); }
uint8_t QuickPID::GetAwMode() const { return static_cast<uint8_t>(iawmode); }
