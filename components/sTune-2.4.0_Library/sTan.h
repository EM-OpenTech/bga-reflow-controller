/**
 * @file sTan.h
 * @brief Sliding tangent line and moving average circular buffer for sTune.
 *
 * Based on the sTune Library for Arduino (v2.4.0) by Dlloydev https://github.com/Dlloydev/sTune
 * Copyright (c) 2022 Dlloydev <dlloydev@testcor.ca>
 * Licensed under the MIT License.
 *
 * ESP-IDF native C++ port by BGA Reflow Controller Team.
 * Ported natively to ESP-IDF v6.0.2 C++ using standard vectors and safe buffers.
 */

#pragma once
#ifndef S_TAN_H_
#define S_TAN_H_

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

#endif // S_TAN_H_
