# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Centralized Compiler Warning & Hardening Matrix for BGA Reflow Controller
# Aligned with MISRA C++:2023, CERT C++, and IEC 60730 Class B

function(target_enable_strict_warnings target_lib)
    get_target_property(target_type ${target_lib} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        set(warning_scope INTERFACE)
    else()
        set(warning_scope PRIVATE)
    endif()

    target_compile_options(${target_lib} ${warning_scope}
        -Wall
        -Wextra
        -Werror
        -Wconversion
        -Wshadow
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wcast-align
        -Wlogical-op
        -Wduplicated-cond
        -Wduplicated-branches
        -Wunused-result
        $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
        $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
    )
    target_compile_definitions(${target_lib} ${warning_scope}
        _FORTIFY_SOURCE=2
    )
endfunction()
