/**
 * @file sTan.cpp
 * @brief Implementation of sliding tangent line and moving average circular buffer.
 *
 * Based on the sTune Library for Arduino (v2.4.0) by Dlloydev https://github.com/Dlloydev/sTune
 * Copyright (c) 2022 Dlloydev <dlloydev@testcor.ca>
 * Licensed under the MIT License.
 *
 * ESP-IDF native C++ port by BGA Reflow Controller Team.
 * Ported natively to ESP-IDF v6.0.2 C++.
 */

#include "sTan.h"

sTan::sTan() : bufSize(0), index(0), sum(0.0f) {}

sTan::sTan(uint16_t bufferSize) : bufSize(bufferSize), index(0), sum(0.0f) {
  begin(bufferSize);
}

void sTan::begin(uint16_t bufferSize) {
  bufSize = bufferSize;
  inputArray.assign(bufSize, 0.0f);
  sTan::init(0.0f);
}

void sTan::init(float reading) {
  index = 0;
  sum = reading * static_cast<float>(bufSize);
  for (uint16_t i = 0; i < bufSize; i++) {
    if (i < inputArray.size()) {
      inputArray[i] = reading;
    }
  }
}

float sTan::avgVal(float reading) {
  if (bufSize == 0 || inputArray.empty()) return reading;
  index++;
  if (index >= bufSize) index = 0;
  sum += reading - inputArray[index];
  inputArray[index] = reading;
  return sum / static_cast<float>(bufSize);
}

float sTan::startVal() const {
  if (bufSize == 0 || inputArray.empty()) return 0.0f;
  uint16_t tailIndex = index + 1;
  if (tailIndex >= bufSize) tailIndex = 0;
  return inputArray[tailIndex];
}

float sTan::slope(float reading) const {
  return reading - sTan::startVal();
}

uint16_t sTan::length() const {
  return bufSize;
}
