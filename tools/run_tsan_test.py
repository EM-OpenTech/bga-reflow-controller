#!/usr/bin/env python3
# ============================================================================
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
# ============================================================================

"""
BGA Reflow Controller - ThreadSanitizer (TSan) Multi-Core Concurrency Runner.
Simulates high-speed concurrent execution between Core 0 (REST API / Telemetry)
and Core 1 (100 Hz Control & Safety loop) over FreeRTOS queues and SystemContext mutex.
Detects data-races, lock inversions, and thread synchronization violations.
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
TSAN_SOURCE = REPO_ROOT / "tools" / "host_test" / "test_concurrency_tsan.cpp"

# Include paths
INCLUDES = [
    "-I", str(MOCKS_DIR),
    "-I", str(REPO_ROOT / "components" / "config" / "include"),
    "-I", str(REPO_ROOT / "components" / "output" / "include"),
    "-I", str(REPO_ROOT / "components" / "safety" / "include"),
    "-I", str(REPO_ROOT / "components" / "pid" / "include"),
    "-I", str(REPO_ROOT / "components" / "QuickPID-3.1.9_Library"),
    "-I", str(REPO_ROOT / "components" / "sTune-2.4.0_Library"),
    "-I", str(REPO_ROOT / "components" / "fsm" / "include"),
    "-I", str(REPO_ROOT / "components" / "sensor" / "include"),
    "-I", str(REPO_ROOT / "components" / "web" / "include"),
    "-I", str(REPO_ROOT / "main" / "include"),
]

# Source files
SOURCES = [
    str(TSAN_SOURCE),
    str(REPO_ROOT / "main" / "src" / "system_context.cpp"),
    str(REPO_ROOT / "components" / "config" / "machine_config.cpp"),
    str(REPO_ROOT / "components" / "output" / "burst_fire.cpp"),
    str(REPO_ROOT / "components" / "output" / "output_manager.cpp"),
    str(REPO_ROOT / "components" / "safety" / "safety_watchdog.cpp"),
    str(REPO_ROOT / "components" / "pid" / "pid_controller.cpp"),
    str(REPO_ROOT / "components" / "pid" / "pid_autotuner.cpp"),
    str(REPO_ROOT / "components" / "QuickPID-3.1.9_Library" / "QuickPID.cpp"),
    str(REPO_ROOT / "components" / "sTune-2.4.0_Library" / "sTan.cpp"),
    str(REPO_ROOT / "components" / "sTune-2.4.0_Library" / "sTune.cpp"),
    str(REPO_ROOT / "components" / "fsm" / "step_runner.cpp"),
    str(REPO_ROOT / "components" / "fsm" / "cooling_controller.cpp"),
    str(REPO_ROOT / "components" / "fsm" / "reflow_fsm.cpp"),
]

def main() -> int:
    print("======================================================================")
    print(" BGA Reflow Controller - ThreadSanitizer Concurrency Guard (TSan)")
    print("======================================================================")

    # 1. Platform check: TSan is supported on Linux, macOS, and Windows WSL2, but not native Windows
    if sys.platform == "win32":
        print("[!] Note: ThreadSanitizer (-fsanitize=thread) is not supported natively by LLVM on Windows.")
        print("    Please run this concurrency guard inside Windows WSL2 or on Linux/macOS.")
        return 0

    # 2. Check for clang++
    cxx = os.environ.get("CXX") or shutil.which("clang++") or "clang++"
    print(f"[*] Using C++ Compiler: {cxx}")

    with tempfile.TemporaryDirectory() as tmpdir:
        binary_path = Path(tmpdir) / "tsan_concurrency_runner"

        cmd = [
            cxx,
            "-std=c++20",
            "-fsanitize=thread",
            "-fno-omit-frame-pointer",
            "-g",
            "-O1",
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

        print("[*] Compiling multi-threaded concurrency harness with TSan active...")
        build_res = subprocess.run(cmd)
        if build_res.returncode != 0:
            print("[!] Compilation failed!", file=sys.stderr)
            return build_res.returncode

        print("[*] Compilation successful. Executing TSan concurrency stress test...")
        print("----------------------------------------------------------------------")
        env = os.environ.copy()
        env["TSAN_OPTIONS"] = "abort_on_error=1:report_bugs=1"

        run_res = subprocess.run([str(binary_path)], env=env)
        return run_res.returncode

if __name__ == "__main__":
    sys.exit(main())
