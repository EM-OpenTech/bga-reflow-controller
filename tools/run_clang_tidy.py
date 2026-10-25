#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# run_clang_tidy.py — Clang-Tidy wrapper for ESP-IDF GCC projects
#
# Problem: clang-tidy uses the Clang frontend to parse code, but ESP-IDF's
# compile_commands.json contains GCC/Xtensa-specific flags that Clang does not
# understand (e.g. -fno-shrink-wrap, -mdisable-hardware-atomics, -Wduplicated-*).
# This script strips those incompatible flags before running clang-tidy, so the
# analysis works cleanly without false "unknown argument" errors.
#
# Usage:
#   python3 tools/run_clang_tidy.py
#   python3 tools/run_clang_tidy.py --fix          # auto-apply fixes
#   python3 tools/run_clang_tidy.py --jobs 8       # parallel jobs
#   python3 tools/run_clang_tidy.py --file main/src/app_controller.cpp

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# ============================================================================
# GCC / Xtensa flags that Clang does not understand
# ============================================================================

# Flags that take no argument — stripped entirely
GCC_ONLY_FLAGS: set[str] = {
    "-fno-shrink-wrap",
    "-fno-jump-tables",
    "-mdisable-hardware-atomics",
    "-mtext-section-literals",
    "-mlongcalls",
    "-Wlogical-op",
    "-Wduplicated-cond",
    "-Wduplicated-branches",
    "-fstack-protector-strong",
    "-fstack-protector",
    "-Wno-error=unused-but-set-variable",
    "-Wno-error=extra",
    "-Wno-enum-conversion",
    "-fno-tree-switch-conversion",
    "-fno-printf-return-value",
    "-fstrict-volatile-bitfields",
}

# Flag prefixes that take an argument — strip the whole pair
GCC_ONLY_PREFIXES: tuple[str, ...] = (
    "-mabi=",
    "-march=xtensa",
    "-mcpu=",
    "-mfix-esp32",
)

# Regex: @"..." response-file references — Clang cannot use GCC response files
RESPONSE_FILE_RE = re.compile(r'@\\?"[^"]*\\?"')

# ============================================================================
# Proprietary source file filter
# ============================================================================

# Only analyse files under these directories (not ESP-IDF itself)
PROPRIETARY_DIRS = (
    "/main/",
    "/components/config/",
    "/components/fsm/",
    "/components/input/",
    "/components/output/",
    "/components/pid/",
    "/components/safety/",
    "/components/sensor/",
    "/components/simulation/",
    "/components/storage/",
    "/components/web/",
    "/components/QuickPID-3.1.9_Library/",
    "/components/sTune-2.4.0_Library/",
)

# Skip test translation units from the firmware scan
SKIP_PATTERNS = (
    "/test/test_",
    "test_runner.cpp",
    "host_main.cpp",
)


def is_proprietary(filepath: str) -> bool:
    """Return True if filepath belongs to our proprietary code."""
    for skip in SKIP_PATTERNS:
        if skip in filepath:
            return False
    for d in PROPRIETARY_DIRS:
        if d in filepath:
            return True
    return False


# ============================================================================
# Flag cleaning
# ============================================================================

def clean_command(command: str) -> str:
    """Strip GCC-only flags and response-file references from a compile command."""
    # 1. Remove @"..." response file references
    command = RESPONSE_FILE_RE.sub("", command)

    tokens = command.split()
    cleaned: list[str] = []
    skip_next = False

    for token in tokens:
        if skip_next:
            skip_next = False
            continue

        if token in GCC_ONLY_FLAGS:
            continue

        if any(token.startswith(p) for p in GCC_ONLY_PREFIXES):
            continue

        # Replace the GCC cross-compiler with a Clang placeholder so that
        # clang-tidy uses its own built-in headers (the compiler path is
        # not actually invoked — clang-tidy only reads the flags).
        if "xtensa-esp32s3-elf-g++" in token or "xtensa-esp-elf-g++" in token:
            token = "clang++"

        # Downgrade gnu++26 → c++20 (clang-tidy host binary may not support gnu++26)
        if token in ("-std=gnu++26", "-std=gnu++23"):
            token = "-std=c++20"

        cleaned.append(token)

    # Add Xtensa GCC/picolibc sysroot headers so Clang can resolve #include_next <stdio.h>
    sysroot_base = Path.home() / ".espressif" / "tools" / "xtensa-esp-elf"
    matches = sorted(sysroot_base.glob("**/picolibc/include"))
    if matches:
        picolibc_inc = matches[0]
        gcc_base = picolibc_inc.parent.parent
        cleaned.extend([
            "-isystem", str(picolibc_inc),
            "-isystem", str(gcc_base / "picolibc" / "xtensa-esp-elf" / "include" / "c++" / "15.2.0" / "xtensa-esp-elf" / "esp32s3"),
            "-isystem", str(gcc_base / "picolibc" / "xtensa-esp-elf" / "include" / "c++" / "15.2.0"),
            "-isystem", str(gcc_base / "lib" / "gcc" / "xtensa-esp-elf" / "15.2.0" / "include"),
            "-isystem", str(gcc_base / "lib" / "gcc" / "xtensa-esp-elf" / "15.2.0" / "include-fixed"),
            "-Wno-unknown-warning-option",
            "-Wno-unknown-argument",
        ])

    return " ".join(cleaned)


# ============================================================================
# Main
# ============================================================================

def find_clang_tidy() -> str:
    """Locate a clang-tidy binary — prefers the Espressif esp-clang toolchain."""
    # 1. Espressif esp-clang (already installed with ESP-IDF, same version as clangd in the IDE)
    import glob
    esp_clang_pattern = str(
        Path.home() / ".espressif" / "tools" / "esp-clang" / "*" / "esp-clang" / "bin" / "clang-tidy"
    )
    esp_matches = sorted(glob.glob(esp_clang_pattern), reverse=True)
    if esp_matches:
        return esp_matches[0]

    # 2. System clang-tidy from PATH
    for name in ["clang-tidy", "clang-tidy-18", "clang-tidy-17", "clang-tidy-16"]:
        result = subprocess.run(["which", name], capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip()

    print(
        "ERROR: clang-tidy not found.\n"
        "  Option A: Install Espressif esp-clang via 'idf_tools.py install esp-clang'\n"
        "  Option B: Install LLVM via 'brew install llvm'",
        file=sys.stderr,
    )
    sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run clang-tidy on ESP-IDF GCC projects without false flag errors."
    )
    parser.add_argument("--build-dir", default="build", help="ESP-IDF build directory (default: build)")
    parser.add_argument("--fix", action="store_true", help="Apply clang-tidy fixes automatically")
    parser.add_argument("--jobs", "-j", type=int, default=4, help="Parallel jobs (default: 4)")
    parser.add_argument("--file", "-f", default=None, help="Analyse only this source file")
    parser.add_argument("--output", "-o", default="clang_tidy_report.txt", help="Write report to file (default: clang_tidy_report.txt)")
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parent.parent
    build_dir = project_root / args.build_dir
    compile_commands_path = build_dir / "compile_commands.json"

    if not compile_commands_path.exists():
        print(f"ERROR: {compile_commands_path} not found. Run 'idf.py build' first.", file=sys.stderr)
        sys.exit(1)

    clang_tidy = find_clang_tidy()
    print(f"[*] Using: {clang_tidy}")
    print(f"[*] Reading: {compile_commands_path}")

    with open(compile_commands_path, encoding="utf-8") as f:
        entries = json.load(f)

    # Filter to proprietary files only
    if args.file:
        target = str(Path(args.file).resolve())
        filtered = [e for e in entries if e["file"] == target or e["file"].endswith(args.file)]
    else:
        filtered = [e for e in entries if is_proprietary(e["file"])]

    if not filtered:
        print("ERROR: No matching source files found in compile_commands.json", file=sys.stderr)
        sys.exit(1)

    print(f"[*] Analysing {len(filtered)} proprietary source files...")

    # Write cleaned compile_commands.json — MUST be named "compile_commands.json"
    # because clang-tidy discovers the compilation database by filename, not by
    # the arbitrary name a NamedTemporaryFile would choose.
    cleaned_entries = []
    for entry in filtered:
        cleaned = dict(entry)
        cleaned["command"] = clean_command(entry["command"])
        cleaned_entries.append(cleaned)

    tmp_dir_obj = tempfile.TemporaryDirectory()
    tmp_dir = tmp_dir_obj.name
    tmp_path = os.path.join(tmp_dir, "compile_commands.json")
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(cleaned_entries, f, indent=2)

    print(f"[*] Cleaned compile_commands written to: {tmp_path}")

    # Build run-clang-tidy command
    run_ct = subprocess.run(["which", "run-clang-tidy"], capture_output=True, text=True)

    try:
        if run_ct.returncode == 0:
            # Preferred: use run-clang-tidy for parallel execution
            cmd = [
                run_ct.stdout.strip(),
                "-clang-tidy-binary", clang_tidy,
                "-p", tmp_dir,
                f"-j{args.jobs}",
                "-header-filter", r"bga-reflow-controller/(components|main)/.*\.(h|hpp)",
            ]
            if args.fix:
                cmd.append("-fix")
            cmd += [e["file"] for e in cleaned_entries]
            print(f"[*] Running: {' '.join(cmd[:4])} ... ({len(cleaned_entries)} files)")
            result = subprocess.run(cmd, text=True)
            sys.exit(result.returncode)
        else:
            # Fallback: run clang-tidy directly on each file (sequential)
            print("[!] run-clang-tidy not found, running sequentially...")
            report_path = project_root / "clang_tidy_report.txt"
            errors = 0
            all_output: list[str] = []
            for entry in cleaned_entries:
                result = subprocess.run(
                    [
                        clang_tidy,
                        f"-p={tmp_dir}",
                        "--header-filter=bga-reflow-controller/(components|main)/.*\\.(h|hpp)",
                        entry["file"],
                    ],
                    capture_output=True,
                    text=True,
                )
                if result.stdout.strip():
                    print(result.stdout)
                    all_output.append(result.stdout)
                if result.returncode != 0:
                    errors += 1

            summary = f"\n[{'OK' if errors == 0 else 'FAIL'}] {len(cleaned_entries)} files, {errors} with issues\n"
            print(summary)
            all_output.append(summary)

            with open(report_path, "w", encoding="utf-8") as f:
                f.writelines(all_output)
            print(f"[*] Full report saved to: {report_path}")

    finally:
        tmp_dir_obj.cleanup()


if __name__ == "__main__":
    main()
