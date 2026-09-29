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
 * @file sTan.hpp
 * @brief Sliding tangent line and moving average circular buffer for sTune.
 *
 * Original:
 *   sTune Library for Arduino - Version 2.4.0 (sTan module)
 *   by dlloydev https://github.com/Dlloydev/sTune
 *   Licensed under the MIT License.
 *
 * ESP-IDF C++ Port:
 *   Ported by EM-OpenTech using standard C++ vectors and safe buffers.
 *
 * @copyright Copyright (c) 2022 Dlloydev <dlloydev@testcor.ca>, MIT License
 * @copyright Copyright (c) 2026 EM-OpenTech (ESP-IDF Port), AGPL-3.0-or-later
 * @see https://github.com/Dlloydev/sTune
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#pragma once
#ifndef S_TAN_HPP_
#define S_TAN_HPP_

#include <cstdint>
#include <vector>
#include <algorithm>

/**
 * @class sTan
 * @brief Circular buffer for computing a sliding tangent line and moving average.
 *
 * Used natively by sTune Inflection Point autotuner in ESP-IDF v6.0.2 C++.
 */
class sTan {
  public:

    /**
     * @brief Default constructor.
     */
    sTan();

    /**
     * @brief Constructor with initial buffer size.
     * @param bufferSize Number of elements in sliding buffer.
     */
    explicit sTan(uint16_t bufferSize);

    /**
     * @brief Default destructor.
     */
    ~sTan() = default;

    /**
     * @brief Initializes buffer with specified size.
     * @param bufferSize Number of elements.
     */
    void begin(uint16_t bufferSize);

    /**
     * @brief Resets buffer and fills all elements with initial reading.
     * @param reading Initial process value.
     */
    void init(float reading);

    /**
     * @brief Pushes a new reading and returns the updated moving average.
     * @param reading New process value.
     * @return Current moving average across buffer.
     */
    float avgVal(float reading);

    /**
     * @brief Returns the oldest value in the sliding window.
     * @return Oldest process value.
     */
    float startVal() const;

    /**
     * @brief Computes tangent slope between newest and oldest reading.
     * @param reading Current process value.
     * @return Tangent delta (slope).
     */
    float slope(float reading) const;

    /**
     * @brief Returns configured buffer size.
     * @return Number of elements in buffer.
     */
    uint16_t length() const;

  private:
    uint16_t bufSize = 0;          ///< Buffer capacity
    uint16_t index = 0;            ///< Current ring buffer insertion index
    float sum = 0.0f;              ///< Running sum of stored elements
    std::vector<float> inputArray; ///< Safe C++ dynamic buffer storage
};

#endif // S_TAN_HPP_
