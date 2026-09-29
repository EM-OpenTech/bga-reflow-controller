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
 * @file sTan.cpp
 * @brief Implementation of sliding tangent line and moving average circular buffer.
 *
 * Based on the sTune Library for Arduino (v2.4.0) by Dlloydev https://github.com/Dlloydev/sTune
 *
 * @copyright Copyright (c) 2022 Dlloydev <dlloydev@testcor.ca>, MIT License
 * @copyright Copyright (c) 2026 EM-OpenTech (ESP-IDF Port), AGPL-3.0-or-later
 * @see https://github.com/Dlloydev/sTune
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "sTan.hpp"

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
