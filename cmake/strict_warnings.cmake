# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Centralized Compiler Warning & Hardening Matrix for BGA Reflow Controller
# Aligned with MISRA C++:2023, CERT C++, and IEC 60730 Class B

function(target_enable_strict_warnings target_lib)
    target_compile_options(${target_lib} PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wconversion
        -Wshadow
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Woverloaded-virtual
        -Wnon-virtual-dtor
        -Wcast-align
        -Wlogical-op
        -Wduplicated-cond
        -Wduplicated-branches
        -Wunused-result
    )
    target_compile_definitions(${target_lib} PRIVATE
        _FORTIFY_SOURCE=2
    )
endfunction()
