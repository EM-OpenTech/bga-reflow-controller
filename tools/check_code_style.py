#!/usr/bin/env python3
# ============================================================================
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
# ============================================================================

"""
BGA Reflow Controller - Static Style & MISRA C++:2023 Scanner.
Automated project-wide static style & sanity scanner.
Scans all C++ header (.hpp/.h) and source (.cpp/.c) files across components/ and main/
for Clang-Tidy / MISRA C++:2023 style compliance:
  1. readability-implicit-bool-conversion (bitwise operations in conditions without explicit != 0)
  2. readability-math-missing-parentheses (mixed arithmetic precedence + / - and * / /)
  3. modernize-use-nullptr (legacy NULL in C++ code)
  4. readability-container-size-empty (.size() == 0 instead of .empty())
  5. readability-simplify-boolean-expr (== true, == false, ? true : false)
  6. readability-comment-style (enforce standardized // === dividers and forbid // ---)
"""

import os
import re
import sys
from pathlib import Path

# Paths to exclude from scanning
EXCLUDE_DIRS = {
    "build",
    ".git",
    ".vscode",
    "managed_components",
    "__pycache__",
}


def strip_comments_and_strings(text: str) -> str:
    """Strip C/C++ comments and string literals while preserving newlines and char offsets."""
    def replacer(match):
        s = match.group(0)
        return "\n" * s.count("\n") + " " * (len(s) - s.count("\n"))

    pattern = re.compile(
        r'//.*?$|/\*.*?\*/|\'(?:\\.|[^\\\'])*\'|\"(?:\\.|[^\\\"])*\"',
        re.DOTALL | re.MULTILINE,
    )
    return re.sub(pattern, replacer, text)


def find_source_files(project_root: Path) -> list[Path]:
    """Find all C/C++ source and header files in components/ and main/."""
    source_files = []
    scan_roots = [project_root / "components", project_root / "main"]

    for root_dir in scan_roots:
        if not root_dir.exists():
            continue
        for root, dirs, files in os.walk(root_dir):
            dirs[:] = [
                d
                for d in dirs
                if d not in EXCLUDE_DIRS
            ]
            for file in files:
                if file.endswith((".cpp", ".hpp", ".c", ".h")):
                    source_files.append(Path(root) / file)

    return sorted(source_files)


def check_implicit_bitwise_bool(clean_content: str, rel_path: str) -> list[str]:
    """Check for bitwise operations used directly in conditionals without != 0."""
    issues = []
    # Match 'if (...)', 'while (...)', '(? ... :)'
    # Simple extraction of paren-enclosed expressions after if/while
    pos = 0
    while True:
        m = re.search(r"\b(if|while)\s*\(", clean_content[pos:])
        if not m:
            break
        start_idx = pos + m.end() - 1
        depth = 0
        end_idx = start_idx
        for i in range(start_idx, len(clean_content)):
            if clean_content[i] == "(":
                depth += 1
            elif clean_content[i] == ")":
                depth -= 1
                if depth == 0:
                    end_idx = i
                    break
        if depth == 0:
            cond = clean_content[start_idx + 1 : end_idx]
            line_no = clean_content[:start_idx].count("\n") + 1
            parts = re.split(r"&&|\|\|", cond)
            for part in parts:
                p_strip = part.strip()
                if "&" in p_strip and not any(op in p_strip for op in ["==", "!=", "<", ">", "<=", ">="]):
                    # Ensure it's a bitwise AND expression (e.g. ident & mask)
                    if re.search(r"[\w\)]\s*&\s*[\w\(]", p_strip):
                        issues.append(
                            f"{rel_path}:{line_no}: [readability-implicit-bool-conversion] "
                            f"Bitwise condition '{p_strip}' used without explicit comparison (use '!= 0')"
                        )
            pos = end_idx + 1
        else:
            pos = start_idx + 1

    return issues


def check_math_missing_parentheses(clean_lines: list[str], rel_path: str) -> list[str]:
    """Check for mixed arithmetic operators (+/- with */%) without explicit enclosing parentheses."""
    issues = []
    var_pattern = r"[a-zA-Z0-9_\.]+"
    pats = [
        re.compile(rf"\b({var_pattern}\s*[\+\-]\s*{var_pattern}\s*[\*/%]\s*{var_pattern})\b"),
        re.compile(rf"\b({var_pattern}\s*[\*/%]\s*{var_pattern}\s*[\+\-]\s*{var_pattern})\b"),
    ]

    for idx, line in enumerate(clean_lines):
        trimmed = line.strip()
        if not trimmed or trimmed.startswith("#"):
            continue

        # Split line by balanced parentheses to check each sub-expression
        depth = 0
        current_segment = []
        segments = []
        for ch in trimmed:
            if ch == "(":
                segments.append("".join(current_segment))
                current_segment = []
                depth += 1
            elif ch == ")":
                segments.append("".join(current_segment))
                current_segment = []
                depth = max(0, depth - 1)
            else:
                current_segment.append(ch)
        segments.append("".join(current_segment))

        for seg in segments:
            # Normalize common safe patterns (pointer types, scientific notation)
            norm = re.sub(r"->|\+\+|--|\+=|-=|\*=|/=|==|!=|<=|>=|<<|>>", "  ", seg)
            norm = re.sub(r"\d+e[\+\-]\d+", "1", norm)
            for pat in pats:
                matches = pat.findall(norm)
                for m in matches:
                    if any(t in m for t in ["char*", "void*", "uint8_t*", "int*", "float*", "auto*", "const "]):
                        continue
                    issues.append(
                        f"{rel_path}:{idx + 1}: [readability-math-missing-parentheses] "
                        f"Mixed arithmetic expression without explicit parentheses: '{m.strip()}'"
                    )

    return issues


def check_modernize_nullptr(clean_lines: list[str], rel_path: str) -> list[str]:
    """Check for legacy NULL instead of nullptr in C++ code."""
    issues = []
    pattern = re.compile(r"\bNULL\b")
    for idx, line in enumerate(clean_lines):
        if line.strip().startswith("#"):
            continue
        if pattern.search(line):
            issues.append(
                f"{rel_path}:{idx + 1}: [modernize-use-nullptr] Use 'nullptr' instead of legacy 'NULL'"
            )
    return issues


def check_container_empty(clean_lines: list[str], rel_path: str) -> list[str]:
    """Check for container.size() == 0 instead of container.empty()."""
    issues = []
    patterns = [
        (re.compile(r"\.size\(\)\s*==\s*0\b"), "Use '.empty()' instead of '.size() == 0'"),
        (re.compile(r"\.length\(\)\s*==\s*0\b"), "Use '.empty()' instead of '.length() == 0'"),
    ]
    for idx, line in enumerate(clean_lines):
        for pat, msg in patterns:
            if pat.search(line):
                issues.append(f"{rel_path}:{idx + 1}: [readability-container-size-empty] {msg}")
    return issues


def check_simplify_boolean(clean_lines: list[str], rel_path: str) -> list[str]:
    """Check for redundant boolean comparisons."""
    issues = []
    pattern = re.compile(r"==\s*true\b|!=\s*true\b|==\s*false\b|!=\s*false\b|\?\s*true\s*:\s*false\b")
    for idx, line in enumerate(clean_lines):
        if pattern.search(line):
            issues.append(
                f"{rel_path}:{idx + 1}: [readability-simplify-boolean-expr] "
                f"Redundant boolean expression: '{line.strip()}'"
            )
    return issues


def check_comment_style(raw_text: str, rel_path: str) -> list[str]:
    """Check comment formatting consistency:
    - Forbid dashed divider lines (// --- or /* ---). Use // 1. ... or // === instead.
    - Standardize section divider lines (// ===...): must match exact 80-column standard (79 chars).
    """
    issues = []
    lines = raw_text.splitlines()
    dash_pattern = re.compile(r"^\s*(?://|/\*)\s*-{5,}")
    equals_pattern = re.compile(r"^\s*//\s*={5,}")

    for idx, line in enumerate(lines, 1):
        if dash_pattern.search(line):
            issues.append(
                f"{rel_path}:{idx}: [readability-comment-style] "
                f"Dashed divider line forbidden. Use '// ===' for major sections or '// 1.' for steps: '{line.strip()}'"
            )
        elif equals_pattern.search(line):
            if len(line) != 79:
                issues.append(
                    f"{rel_path}:{idx}: [readability-comment-style] "
                    f"Section divider line length is {len(line)}, must be exactly 79 characters (80-column standard): '{line.strip()}'"
                )
    return issues


def check_system_context_snapshot_safety(project_root: Path) -> list[str]:
    """Ensure SystemContextData never contains unbounded heap-allocating containers.

    Live 2 Hz snapshots must remain lightweight POD/scalars only to prevent
    heap fragmentation and memory allocation starvation on Core 0.
    """
    issues = []
    header = project_root / "main" / "include" / "system_context.hpp"
    if not header.exists():
        return issues

    content = header.read_text(encoding="utf-8")
    # Extract struct SystemContextData body
    match = re.search(r"struct\s+SystemContextData\s*\{([^}]+)\};", content, re.DOTALL)
    if not match:
        issues.append("main/include/system_context.hpp: could not locate struct SystemContextData")
        return issues

    body = match.group(1)
    forbidden_types = [
        ("std::deque", "std::deque"),
        ("std::list", "std::list"),
        ("std::map", "std::map"),
        ("std::set", "std::set"),
        ("std::unordered_map", "std::unordered_map"),
        ("history", "history container"),
    ]
    for pattern, name in forbidden_types:
        if pattern in body:
            issues.append(
                f"main/include/system_context.hpp: [snapshot-safety] "
                f"Forbidden {name} detected in SystemContextData! "
                f"Live 2 Hz snapshots must remain lightweight scalars/POD only."
            )

    # Check for any std::vector that is not stepMarkers
    vectors = re.findall(r"std::vector<([^>]+)>\s+(\w+)", body)
    for vtype, vname in vectors:
        if vname != "stepMarkers":
            issues.append(
                f"main/include/system_context.hpp: [snapshot-safety] "
                f"Forbidden dynamic container 'std::vector<{vtype}> {vname}' in SystemContextData! "
                f"Move historical/bulk containers out of SystemContextData."
            )

    return issues


def check_kconfig_safety_invariants(project_root: Path) -> list[str]:
    """Ensure critical hardware & watchdog invariants remain active in sdkconfig.defaults."""
    issues = []
    defaults = project_root / "sdkconfig.defaults"
    if not defaults.exists():
        return issues

    content = defaults.read_text(encoding="utf-8")

    if "CONFIG_ESP_TASK_WDT_PANIC=y" not in content:
        issues.append(
            "sdkconfig.defaults: [kconfig-safety] "
            "Missing 'CONFIG_ESP_TASK_WDT_PANIC=y'! Task watchdog must panic to capture coredumps."
        )

    psram_match = re.search(r"CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=(\d+)", content)
    if not psram_match:
        issues.append(
            "sdkconfig.defaults: [kconfig-safety] "
            "Missing 'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL'! Must be defined."
        )
    else:
        val = int(psram_match.group(1))
        if val > 16384:
            issues.append(
                f"sdkconfig.defaults: [kconfig-safety] "
                f"CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL={val} exceeds 16384 (16 KB)! "
                f"Lower threshold to <= 16384 so large buffers migrate to Octal PSRAM."
            )

    return issues


def main() -> int:
    project_root = Path(__file__).resolve().parent.parent
    files = find_source_files(project_root)

    print("=" * 70)
    print(" BGA Reflow Controller - Static Style & Clang-Tidy Scanner")
    print("=" * 70)
    print(f"Scanning {len(files)} source & header files in components/ and main/...\n")

    all_issues = []

    # Architectural invariant guards
    all_issues.extend(check_system_context_snapshot_safety(project_root))
    all_issues.extend(check_kconfig_safety_invariants(project_root))

    for file_path in files:
        rel_path = file_path.relative_to(project_root).as_posix()
        try:
            raw_text = file_path.read_text(encoding="utf-8", errors="ignore")
        except Exception as e:
            print(f"[ERROR] Could not read {rel_path}: {e}")
            continue

        clean_text = strip_comments_and_strings(raw_text)
        clean_lines = clean_text.split("\n")

        all_issues.extend(check_comment_style(raw_text, rel_path))
        all_issues.extend(check_implicit_bitwise_bool(clean_text, rel_path))
        all_issues.extend(check_math_missing_parentheses(clean_lines, rel_path))
        all_issues.extend(check_modernize_nullptr(clean_lines, rel_path))
        all_issues.extend(check_container_empty(clean_lines, rel_path))
        all_issues.extend(check_simplify_boolean(clean_lines, rel_path))

    if all_issues:
        print(f"FAILED: Found {len(all_issues)} style issue(s):\n")
        for issue in all_issues:
            print(f"  - {issue}")
        print("\nPlease fix the above issues according to .clang-tidy guidelines.")
        return 1

    print(f"SUCCESS: All {len(files)} files checked. 0 issues found! Code style is 100% compliant.")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
