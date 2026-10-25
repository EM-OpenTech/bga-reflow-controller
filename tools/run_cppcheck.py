#!/usr/bin/env python3
# ============================================================================
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
# ============================================================================

"""
BGA Reflow Controller - Cppcheck Static Analysis Runner.
Executes an external secondary static analysis scan using Cppcheck across
all proprietary C++20 components and main task logic using compile_commands.json.
"""

import datetime
import os
import shutil
import subprocess
import sys
from pathlib import Path

# Paths
REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "build"
COMPILE_COMMANDS = BUILD_DIR / "compile_commands.json"
REPORT_FILE = REPO_ROOT / "cppcheck_report.txt"


def find_cppcheck() -> str | None:
    """Find cppcheck executable on host system."""
    # 1. System PATH
    found = shutil.which("cppcheck")
    if found:
        return found

    # 2. Common macOS, Linux & Windows installation paths
    common_paths = [
        "/opt/homebrew/bin/cppcheck",
        "/usr/local/bin/cppcheck",
        "/usr/bin/cppcheck",
        r"C:\Program Files\Cppcheck\cppcheck.exe",
        r"C:\Program Files (x86)\Cppcheck\cppcheck.exe",
    ]
    for p in common_paths:
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p

    return None


def main() -> int:
    print("======================================================================")
    print(" BGA Reflow Controller - Cppcheck Secondary Static Analysis Guard")
    print("======================================================================")

    # 1. Verify Cppcheck binary
    cppcheck_bin = find_cppcheck()
    if not cppcheck_bin:
        print("[!] ERROR: 'cppcheck' is not installed or not in PATH.")
        print("    To install on macOS via Homebrew, run:")
        print("      brew install cppcheck")
        print("    To install on Debian/Ubuntu, run:")
        print("      sudo apt-get install cppcheck")
        return 1

    # Check cppcheck version
    try:
        ver_res = subprocess.run([cppcheck_bin, "--version"], capture_output=True, text=True, check=True)
        version_str = ver_res.stdout.strip()
        print(f"[*] Found Analyzer: {version_str} ({cppcheck_bin})")
    except Exception as e:
        print(f"[!] Warning: Could not query cppcheck version: {e}")

    # 2. Check compilation database
    if not COMPILE_COMMANDS.exists():
        print(f"[!] Compilation database not found at: {COMPILE_COMMANDS}")
        print("    Please build the project first (e.g. 'ninja -C build' or 'idf.py reconfigure')")
        print("    so that compile_commands.json is generated.")
        return 1

    jobs = os.cpu_count() or 4
    print(f"[*] Using Compilation Database: {COMPILE_COMMANDS}")
    print(f"[*] Running multi-core control-flow & data-flow analysis ({jobs} parallel workers)...")
    print("----------------------------------------------------------------------")

    cmd = [
        cppcheck_bin,
        f"-j{jobs}",
        f"--project={COMPILE_COMMANDS}",
        "--enable=warning,style,performance,portability",
        "--std=c++20",
        "--inline-suppr",
        "--suppress=missingIncludeSystem",
        "--suppress=missingInclude",
        "--suppress=missingFile",
        "--suppress=unmatchedSuppression",
        "--suppress=*:*managed_components*",
        "--suppress=*:*espressif__*",
        "-i", str(REPO_ROOT / "managed_components"),
        "-i", str(REPO_ROOT / "build"),
        "--template={file}:{line}: [{severity}] ({id}) {message}",
        "--error-exitcode=1",
    ]

    try:
        res = subprocess.run(cmd, cwd=str(REPO_ROOT), capture_output=True, text=True)
    except Exception as e:
        print(f"[!] Execution failed: {e}")
        return 1

    # Cppcheck writes diagnostic findings to stderr
    output_lines = [line for line in (res.stdout + res.stderr).splitlines() if line.strip()]

    # Filter out pure informational status lines
    issues = [
        line for line in output_lines
        if not line.startswith("Checking ")
        and not "files checked" in line
        and not "Defines:" in line
        and not "Active checkers:" in line
    ]

    timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    report_content = [
        "======================================================================",
        f" BGA Reflow Controller - Cppcheck Static Analysis Report",
        f" Date: {timestamp}",
        f" Tool: {version_str}",
        "======================================================================\n",
    ]

    if issues:
        report_content.append(f"Result: FAILED - {len(issues)} issue(s) detected:\n")
        for issue in issues:
            report_content.append(f"  {issue}")
        report_content.append("\n======================================================================")
        REPORT_FILE.write_text("\n".join(report_content) + "\n", encoding="utf-8")

        print(f"[!] Found {len(issues)} issue(s) during Cppcheck analysis:\n")
        for issue in issues:
            print(f"    {issue}")
        print(f"\n[*] Full log saved to: {REPORT_FILE.name}")
        print("======================================================================")
        print(" Cppcheck Scan Finished: Issues detected. Please review findings above.")
        print("======================================================================")
        return 1

    report_content.append("Result: SUCCESS - ZERO issues detected!\n")
    report_content.append("All components & tasks compliant with C++20 safety standards.")
    report_content.append("======================================================================")
    REPORT_FILE.write_text("\n".join(report_content) + "\n", encoding="utf-8")

    print("[+] Cppcheck Scan Complete: ZERO issues detected!")
    print(f"[*] Full log saved to: {REPORT_FILE.name}")
    print("======================================================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
