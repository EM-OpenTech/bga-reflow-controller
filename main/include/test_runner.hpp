#pragma once

namespace app {

/**
 * @brief Unified Test Runner for ESP-IDF v6.0.2.
 * Runs Unity unit tests for all 11 modules and outputs results to UART.
 * @return true if all unit tests passed, false if any failures occurred.
 */
bool runAllUnitTests();

} // namespace app
