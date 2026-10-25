#!/usr/bin/env python3
# ============================================================================
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
# ============================================================================

"""
BGA Reflow Controller - Code Coverage Measurement Tool.
Compiles native host test suite with LLVM source-based code coverage
(-fprofile-instr-generate -fcoverage-mapping), executes tests, merges
runtime profile traces, and generates detailed line/branch coverage reports.
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# Import canonical sources and include definitions from host test runner
REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools"))
from run_host_tests import INCLUDES, SOURCES  # type: ignore

def main() -> int:
    parser = argparse.ArgumentParser(description="Generate native code coverage reports via LLVM")
    parser.add_argument("--html", action="store_true", help="Generate detailed HTML coverage report")
    parser.add_argument("--outdir", default=str(REPO_ROOT / "build" / "coverage"), help="Output directory for HTML report")
    args = parser.parse_args()

    print("======================================================================")
    print(" BGA Reflow Controller - LLVM Source-Based Code Coverage Guard")
    print("======================================================================")

    cxx = os.environ.get("CXX", "clang++")
    print(f"[*] Using Compiler: {cxx}")

    with tempfile.TemporaryDirectory() as tmpdir:
        binary_path = Path(tmpdir) / "coverage_runner"
        profraw_path = Path(tmpdir) / "test.profraw"
        profdata_path = Path(tmpdir) / "test.profdata"

        compile_cmd = [
            cxx,
            "-std=c++20",
            "-fprofile-instr-generate",
            "-fcoverage-mapping",
            "-O0",
            "-g",
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

        print("[*] Compiling test suite with coverage instrumentation active...")
        build_res = subprocess.run(compile_cmd)
        if build_res.returncode != 0:
            print("[!] Compilation failed!", file=sys.stderr)
            return build_res.returncode

        print("[*] Executing test runner to record execution traces...")
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = str(profraw_path)

        run_res = subprocess.run([str(binary_path)], env=env)
        if run_res.returncode != 0:
            print("[!] Test execution failed under coverage runner!", file=sys.stderr)
            return run_res.returncode

        print("[*] Merging profile traces with llvm-profdata...")
        merge_cmd = ["xcrun", "llvm-profdata", "merge", "-sparse", str(profraw_path), "-o", str(profdata_path)]
        merge_res = subprocess.run(merge_cmd)
        if merge_res.returncode != 0:
            print("[!] Profile merge failed!", file=sys.stderr)
            return merge_res.returncode

        print("\n----------------------------------------------------------------------")
        print(" COMPONENT CODE COVERAGE REPORT (Filtered: core business logic)")
        print("----------------------------------------------------------------------")

        ignore_regex = r"(mocks|unity|managed_components|test_)"
        report_cmd = [
            "xcrun",
            "llvm-cov",
            "report",
            str(binary_path),
            f"-instr-profile={profdata_path}",
            f"-ignore-filename-regex={ignore_regex}",
        ]
        subprocess.run(report_cmd)

        if args.html:
            out_path = Path(args.outdir)
            out_path.mkdir(parents=True, exist_ok=True)
            html_cmd = [
                "xcrun",
                "llvm-cov",
                "show",
                str(binary_path),
                f"-instr-profile={profdata_path}",
                f"-ignore-filename-regex={ignore_regex}",
                "-format=html",
                f"-output-dir={out_path}",
            ]
            print(f"\n[*] Generating interactive HTML report in: {out_path} ...")
            subprocess.run(html_cmd)
            print(f"[+] HTML coverage report ready: {out_path / 'index.html'}")

    print("======================================================================")
    return 0

if __name__ == "__main__":
    sys.exit(main())
