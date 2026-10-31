// SPDX-FileCopyrightText: 2026 EM-OpenTech
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#define UNITY_INCLUDE_FLOAT
#define UNITY_INCLUDE_DOUBLE

#ifdef isnan
#undef isnan
#endif

#ifdef isinf
#undef isinf
#endif

#ifndef TEST_ASSERT_GREATER_THAN_FLOAT
#define TEST_ASSERT_GREATER_THAN_FLOAT(threshold, actual) TEST_ASSERT_TRUE((actual) > (threshold))
#endif

#ifndef TEST_ASSERT_LESS_THAN_FLOAT
#define TEST_ASSERT_LESS_THAN_FLOAT(threshold, actual) TEST_ASSERT_TRUE((actual) < (threshold))
#endif
