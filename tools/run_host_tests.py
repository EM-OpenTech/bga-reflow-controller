#!/usr/bin/env python3
# ============================================================================
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
# ============================================================================

"""
BGA Reflow Controller - Host Unit Test Runner with Clang Sanitizers.
Compiles and executes core business logic natively on host CPU using:
  - Clang AddressSanitizer (ASan): Memory corruption, buffer overflows, use-after-free
  - Clang UndefinedBehaviorSanitizer (UBSan): Division by zero, float overflow, alignment
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Paths
REPO_ROOT = Path(__file__).resolve().parent.parent
MOCKS_DIR = REPO_ROOT / "tools" / "host_test" / "mocks"
UNITY_DIR = REPO_ROOT / "tools" / "host_test" / "unity"
HOST_MAIN = REPO_ROOT / "tools" / "host_test" / "host_main.cpp"

# Include paths
INCLUDES = [
    "-I", str(MOCKS_DIR),
    "-I", str(UNITY_DIR),
    "-I", str(REPO_ROOT / "components" / "config" / "include"),
    "-I", str(REPO_ROOT / "components" / "output" / "include"),
    "-I", str(REPO_ROOT / "components" / "safety" / "include"),
    "-I", str(REPO_ROOT / "components" / "pid" / "include"),
    "-I", str(REPO_ROOT / "components" / "QuickPID-3.1.9_Library"),
    "-I", str(REPO_ROOT / "components" / "sTune-2.4.0_Library"),
    "-I", str(REPO_ROOT / "components" / "fsm" / "include"),
    "-I", str(REPO_ROOT / "components" / "simulation" / "include"),
    "-I", str(REPO_ROOT / "components" / "sensor" / "include"),
    "-I", str(REPO_ROOT / "components" / "input" / "include"),
    "-I", str(REPO_ROOT / "components" / "storage" / "include"),
    "-I", str(REPO_ROOT / "components" / "web" / "include"),
    "-I", str(REPO_ROOT / "main" / "include"),
    "-I", str(REPO_ROOT / "managed_components" / "espressif__cjson" / "cJSON"),
]

# Source files
SOURCES = [
    str(UNITY_DIR / "unity.c"),
    str(HOST_MAIN),
    # Config
    str(REPO_ROOT / "components" / "config" / "machine_config.cpp"),
    str(REPO_ROOT / "components" / "config" / "test" / "test_config.cpp"),
    # Output
    str(REPO_ROOT / "components" / "output" / "burst_fire.cpp"),
    str(REPO_ROOT / "components" / "output" / "output_manager.cpp"),
    str(REPO_ROOT / "components" / "output" / "test" / "test_output.cpp"),
    # Safety
    str(REPO_ROOT / "components" / "safety" / "safety_watchdog.cpp"),
    str(REPO_ROOT / "components" / "safety" / "test" / "test_safety.cpp"),
    # PID & Tuners
    str(REPO_ROOT / "components" / "QuickPID-3.1.9_Library" / "QuickPID.cpp"),
    str(REPO_ROOT / "components" / "sTune-2.4.0_Library" / "sTan.cpp"),
    str(REPO_ROOT / "components" / "sTune-2.4.0_Library" / "sTune.cpp"),
    str(REPO_ROOT / "components" / "pid" / "pid_controller.cpp"),
    str(REPO_ROOT / "components" / "pid" / "pid_autotuner.cpp"),
    str(REPO_ROOT / "components" / "pid" / "test" / "test_pid.cpp"),
    str(REPO_ROOT / "components" / "pid" / "test" / "test_autotuner.cpp"),
    # FSM
    str(REPO_ROOT / "components" / "fsm" / "step_runner.cpp"),
    str(REPO_ROOT / "components" / "fsm" / "cooling_controller.cpp"),
    str(REPO_ROOT / "components" / "fsm" / "reflow_fsm.cpp"),
    str(REPO_ROOT / "components" / "fsm" / "test" / "test_fsm.cpp"),
    # Simulation
    str(REPO_ROOT / "components" / "simulation" / "thermal_simulator.cpp"),
    str(REPO_ROOT / "components" / "simulation" / "test" / "test_simulation.cpp"),
    # Sensor
    str(REPO_ROOT / "components" / "sensor" / "max31856.cpp"),
    str(REPO_ROOT / "components" / "sensor" / "test" / "test_sensor.cpp"),
    # Input
    str(REPO_ROOT / "components" / "input" / "input_manager.cpp"),
    str(REPO_ROOT / "components" / "input" / "test" / "test_input.cpp"),
    # Storage & JSON
    str(REPO_ROOT / "managed_components" / "espressif__cjson" / "cJSON" / "cJSON.c"),
    str(REPO_ROOT / "components" / "storage" / "storage_manager.cpp"),
    str(REPO_ROOT / "components" / "storage" / "json_serializer.cpp"),
    str(REPO_ROOT / "components" / "storage" / "test" / "test_storage.cpp"),
    # Web & Security
    str(REPO_ROOT / "components" / "web" / "src" / "api_context.cpp"),
    str(REPO_ROOT / "components" / "web" / "src" / "system_routes.cpp"),
    str(REPO_ROOT / "components" / "web" / "src" / "control_routes.cpp"),
    str(REPO_ROOT / "components" / "web" / "src" / "ota_routes.cpp"),
    str(REPO_ROOT / "components" / "web" / "src" / "profile_routes.cpp"),
    str(REPO_ROOT / "components" / "web" / "src" / "static_file_server.cpp"),
    str(REPO_ROOT / "components" / "web" / "src" / "ws_handler.cpp"),
    str(REPO_ROOT / "components" / "web" / "src" / "wifi_manager.cpp"),
    str(REPO_ROOT / "components" / "web" / "src" / "web_server.cpp"),
    str(REPO_ROOT / "components" / "web" / "src" / "rest_api.cpp"),
    str(REPO_ROOT / "components" / "web" / "test" / "test_web.cpp"),
    # Main Integration & SystemContext
    str(REPO_ROOT / "main" / "src" / "system_context.cpp"),
    str(REPO_ROOT / "main" / "src" / "fsm_command_queue.cpp"),
    str(REPO_ROOT / "main" / "test" / "test_main_integration.cpp"),
]

def main() -> int:
    print("======================================================================")
    print(" BGA Reflow Controller - Host Sanitizer Test Runner (ASan + UBSan)")
    print("======================================================================")

    # 1. Check for clang++
    cxx = os.environ.get("CXX") or shutil.which("clang++") or "clang++"
    print(f"[*] Using C++ Compiler: {cxx}")

    with tempfile.TemporaryDirectory() as tmpdir:
        binary_name = "host_test_runner.exe" if sys.platform == "win32" else "host_test_runner"
        binary_path = Path(tmpdir) / binary_name

        cmd = [
            cxx,
            "-std=c++20",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-g",
            "-O1",
            "-DHOST_TEST=1",
            "-Wall",
            "-Wextra",
            "-Wno-deprecated",
            "-Wno-unused-variable",
            "-Wno-unused-parameter",
            "-Wno-unused-const-variable",
            "-Wno-unused-private-field",
            "-Wno-unused-but-set-variable",
            "-Wno-deprecated-declarations",
        ] + INCLUDES + SOURCES + ["-o", str(binary_path)]

        print("[*] Compiling 24 C++/C translation units with Sanitizers active...")
        build_res = subprocess.run(cmd)
        if build_res.returncode != 0:
            print("[!] Compilation failed!", file=sys.stderr)
            return build_res.returncode

        print("[*] Compilation successful. Executing test runner...")
        print("----------------------------------------------------------------------")
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "abort_on_error=1"
        env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"

        run_res = subprocess.run([str(binary_path)], env=env)
        return run_res.returncode

if __name__ == "__main__":
    sys.exit(main())
