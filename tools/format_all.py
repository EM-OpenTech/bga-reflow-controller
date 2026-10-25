#!/usr/bin/env python3
"""
SPDX-FileCopyrightText: 2026 EM-OpenTech
SPDX-License-Identifier: AGPL-3.0-or-later

tools/format_all.py
Runs clang-format over all C/C++ source and header files in the project
(including QuickPID and sTune libraries; excluding managed_components).
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

EXCLUDE_DIRS = {
    "build",
    ".git",
    ".vscode",
    "managed_components",
    "__pycache__",
}

def find_clang_format() -> str:
    candidates = []

    # 1. System PATH
    which_cf = shutil.which("clang-format")
    if which_cf:
        candidates.append(which_cf)

    # 2. Dynamic lookup in ESP-IDF tools directory (~/.espressif/tools/esp-clang/.../bin/clang-format)
    tools_base = Path(os.environ.get("IDF_TOOLS_PATH", Path.home() / ".espressif"))
    esp_clang_dir = tools_base / "tools" / "esp-clang"
    if esp_clang_dir.is_dir():
        for p in sorted(esp_clang_dir.glob("**/bin/clang-format*"), reverse=True):
            if p.is_file() and os.access(p, os.X_OK):
                candidates.append(str(p))

    # 3. Standard Windows & Unix installation paths
    for std_path in [
        "/opt/homebrew/bin/clang-format",
        "/usr/local/bin/clang-format",
        "/usr/bin/clang-format",
        "/Library/Developer/CommandLineTools/usr/bin/clang-format",
        r"C:\Program Files\LLVM\bin\clang-format.exe",
    ]:
        candidates.append(std_path)

    for candidate in candidates:
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

    raise RuntimeError(
        "clang-format executable not found! Checked: "
        + ", ".join(candidates)
    )


def find_files_to_format(project_root: Path) -> list[Path]:
    files = []
    for sub in ["components", "main"]:
        folder = project_root / sub
        if not folder.exists():
            continue
        for root, dirs, filenames in os.walk(folder):
            dirs[:] = [
                d
                for d in dirs
                if d not in EXCLUDE_DIRS
            ]
            for f in filenames:
                if f.endswith((".cpp", ".hpp", ".c", ".h")):
                    files.append(Path(root) / f)
    return sorted(files)


def main() -> int:
    project_root = Path(__file__).resolve().parent.parent
    clang_format = find_clang_format()
    files = find_files_to_format(project_root)

    print("=" * 70)
    print(" BGA Reflow Controller - Clang-Format Runner")
    print("=" * 70)
    print(f"Using binary: {clang_format}")
    print(f"Formatting {len(files)} files...\n")

    cmd = [clang_format, "-i"] + [str(f) for f in files]
    ret = subprocess.run(cmd)

    if ret.returncode != 0:
        print(f"[ERROR] clang-format failed with exit code {ret.returncode}")
        return ret.returncode

    print(f"SUCCESS: Successfully formatted {len(files)} files with .clang-format.")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
