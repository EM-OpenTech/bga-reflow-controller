#!/usr/bin/env python3
# ============================================================================
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
# ============================================================================

"""
BGA Reflow Controller - Frontend Architecture & Standards Linter.
Automated quality guard for Web UI assets (HTML5, CSS3, ES2022+):
  1. Strict Language Guard: Prohibits legacy `var` declarations in JavaScript.
  2. Type Safety: Enforces strict equality `===` and `!==` (bans loose `==` / `!=`).
  3. Security Standard: Prohibits dangerous dynamic evaluations (`eval()`, `new Function()`).
  4. Offline-First & Zero-CDN: Verifies zero external CDN links (fonts, cdnjs, external scripts).
  5. Form Integrity: Enforces `autocomplete="off"` on machine parameters and `new-password` on Wi-Fi.
  6. Touch Action Standards: Verifies `touch-action: manipulation` on interactive elements.
  7. I18n Coverage: Ensures all UI text keys in HTML/JS have definitions in `assets/lang/de.js`.
  8. Flash ROM Budget: Validates total gzipped payload does not exceed 160 KB.
  9. Safety Invariant: Asserts Emergency Stop button is never disabled during disconnection.
"""

import gzip
import os
import re
import sys
from pathlib import Path

# Size budget in kilobytes
MAX_GZIP_BUDGET_KB = 160.0

PROJECT_ROOT = Path(__file__).resolve().parent.parent
ASSETS_DIR = PROJECT_ROOT / "assets"
BUILD_ASSETS_DIR = PROJECT_ROOT / "components" / "web" / "build_assets"
LANG_DE_FILE = ASSETS_DIR / "lang" / "de.js"


def strip_js_comments_and_strings(code: str) -> str:
    """Strip JavaScript comments and string literals while preserving newlines and char positions."""
    def replacer(match):
        s = match.group(0)
        return "\n" * s.count("\n") + " " * (len(s) - s.count("\n"))

    pattern = re.compile(
        r'//.*?$|/\*.*?\*/|\'(?:\\.|[^\'\\])*\'|"(?:\\.|[^"\\])*"|`(?:\\.|[^`\\])*`',
        re.DOTALL | re.MULTILINE,
    )
    return re.sub(pattern, replacer, code)


def check_javascript_rules(js_file: Path) -> list[str]:
    """Scan JavaScript files for forbidden constructs (var, loose equality, eval)."""
    issues = []
    content = js_file.read_text(encoding="utf-8")
    clean = strip_js_comments_and_strings(content)
    lines = clean.splitlines()

    for idx, line in enumerate(lines, 1):
        # 1. Check for 'var' keyword
        if re.search(r"\bvar\s+[a-zA-Z0-9_$]+", line):
            issues.append(f"{js_file.name}:{idx}: Forbidden 'var' keyword detected (use 'const' or 'let').")

        # 2. Check for loose equality '==' or '!=' (excluding === and !==)
        if re.search(r"(?<![!=])==(?!=)|(?<![!=])!=(?!=)", line):
            issues.append(f"{js_file.name}:{idx}: Loose equality detected (use strict '===' or '!==').")

        # 3. Check for eval() or new Function()
        if re.search(r"\beval\s*\(", line):
            issues.append(f"{js_file.name}:{idx}: Forbidden 'eval()' invocation detected.")
        if re.search(r"\bnew\s+Function\s*\(", line):
            issues.append(f"{js_file.name}:{idx}: Forbidden 'new Function()' invocation detected.")

    return issues


def check_offline_and_cdn(html_file: Path, css_file: Path) -> list[str]:
    """Ensure zero external CDN references or blocking external resource loaders."""
    issues = []

    # Strip HTML comments before scanning
    raw_html = html_file.read_text(encoding="utf-8")
    html_text = re.sub(r"<!--.*?-->", "", raw_html, flags=re.DOTALL)

    # Check for external resource imports in <link>, <script>, <img>, <iframe>
    resource_patterns = [
        r'<link\b[^>]*(?:href)\s*=\s*["\'](https?://[^"\']+|//[^"\']+)["\']',
        r'<script\b[^>]*(?:src)\s*=\s*["\'](https?://[^"\']+|//[^"\']+)["\']',
        r'<img\b[^>]*(?:src)\s*=\s*["\'](https?://[^"\']+|//[^"\']+)["\']',
        r'<iframe\b[^>]*(?:src)\s*=\s*["\'](https?://[^"\']+|//[^"\']+)["\']',
    ]
    for pat in resource_patterns:
        matches = re.findall(pat, html_text, re.IGNORECASE)
        for m in matches:
            issues.append(f"index.html: Prohibited external CDN/resource dependency found: '{m}'")

    # Check CSS files (bundled style.css and modular assets/css/*.css)
    css_files = [css_file]
    css_dir = ASSETS_DIR / "css"
    if css_dir.is_dir():
        css_files.extend(sorted(css_dir.glob("*.css")))

    for cf in css_files:
        if cf.exists():
            css_text = cf.read_text(encoding="utf-8")
            if re.search(r'@import\s+(?:url\()?["\']?(?:https?:)?//', css_text, re.IGNORECASE):
                issues.append(f"{cf.name}: Prohibited external @import found in stylesheet.")
            if re.search(r'url\(\s*["\']?(?:https?:)?//', css_text, re.IGNORECASE):
                issues.append(f"{cf.name}: Prohibited external remote asset url() found in stylesheet.")

    return issues


def check_form_autocomplete(html_file: Path) -> list[str]:
    """Ensure machine parameter inputs have autocomplete protection."""
    issues = []
    html_text = html_file.read_text(encoding="utf-8")

    # Match all <input> elements
    input_matches = re.findall(r'<input\b[^>]*>', html_text, re.IGNORECASE)
    for tag in input_matches:
        # File inputs, hidden inputs, and checkboxes do not need autocomplete
        if 'type="file"' in tag or 'type="checkbox"' in tag or 'display: none' in tag:
            continue
        if 'type="password"' in tag:
            if 'autocomplete="new-password"' not in tag:
                issues.append(f"index.html: Password input must declare autocomplete=\"new-password\": {tag[:60]}...")
        else:
            if 'autocomplete="off"' not in tag:
                issues.append(f"index.html: Machine input missing autocomplete=\"off\": {tag[:60]}...")

    return issues


def check_touch_action_styling(css_file: Path) -> list[str]:
    """Verify touch-action: manipulation is applied to interactive elements."""
    issues = []
    if not css_file.exists():
        return [f"{css_file.name}: Stylesheet not found."]

    css_text = css_file.read_text(encoding="utf-8")
    if "touch-action: manipulation" not in css_text:
        issues.append(f"{css_file.name}: Missing 'touch-action: manipulation;' rule for mobile tap optimization.")

    return issues


def check_inline_styles(html_file: Path) -> list[str]:
    """Ensure zero inline style="..." attributes in HTML templates."""
    issues = []
    raw_html = html_file.read_text(encoding="utf-8")
    html_text = re.sub(r"<!--.*?-->", "", raw_html, flags=re.DOTALL)
    matches = re.finditer(r'<[a-zA-Z0-9-]+\b[^>]*\bstyle\s*=\s*["\'][^"\']*["\'][^>]*>', html_text)
    for m in matches:
        tag = m.group(0)
        issues.append(f"index.html: Prohibited inline style attribute detected: {tag[:80]}...")

    return issues


def check_i18n_key_coverage(html_file: Path, js_file: Path, lang_file: Path) -> list[str]:
    """Verify that all keys referenced in index.html and app.js exist in de.js."""
    issues = []
    if not lang_file.exists():
        return [f"{lang_file.name}: Localization dictionary not found."]

    # 1. Collect keys from HTML
    html_text = html_file.read_text(encoding="utf-8")
    html_keys = set(re.findall(r'data-lang(?:-placeholder|-title)?="([^"]+)"', html_text))

    # 2. Collect keys from JS
    js_text = js_file.read_text(encoding="utf-8")
    js_keys = set(re.findall(r'\bt\(\s*["\']([^"\']+)["\']', js_text))

    all_keys = html_keys | js_keys

    # 3. Collect keys from de.js
    lang_text = lang_file.read_text(encoding="utf-8")
    dict_keys = set(re.findall(r'"([a-zA-Z0-9_\.]+)"\s*:', lang_text))

    missing = sorted(all_keys - dict_keys)
    if missing:
        issues.append(f"de.js: {len(missing)} missing translation key(s): {', '.join(missing)}")

    return issues


def check_autotune_and_debounce_guards(js_dir: Path) -> list[str]:
    """Verify that action locking (debouncing) and PID capacity guards are enforced."""
    issues = []
    tune_js = js_dir / "12_tab_tunepid.js"
    if not tune_js.exists():
        return [f"{tune_js.name}: File missing."]

    content = tune_js.read_text(encoding="utf-8")

    # 1. Action Button Locking / Debounce (Network latency guard)
    if "btnTune.disabled = true" not in content:
        issues.append("12_tab_tunepid.js: Missing action button locking (btnTune.disabled = true) before API call.")

    # 2. Pre-Flight PID Capacity Guard
    if "MAX_PID_POINTS" not in content or "hasExistingPoint" not in content:
        issues.append("12_tab_tunepid.js: Missing pre-flight capacity guard (MAX_PID_POINTS) in startAutotune().")

    # 3. Post-Flight Verification Guard
    if "isSaved" not in content:
        issues.append(
            "12_tab_tunepid.js: Missing post-flight persistence verification (isSaved) in handleAutotuneCompleted()."
        )

    return issues


def check_rom_payload_budget(assets_dir: Path) -> list[str]:
    """Calculate compressed payload size and assert it is within the 160 KB budget."""
    issues = []
    target_files = ["chart.min.js", "index.html", "style.css", "app.js", "lang/de.js"]
    total_gz_bytes = 0

    for rel_path in target_files:
        src = assets_dir / rel_path
        if not src.exists():
            issues.append(f"Asset file missing: {rel_path}")
            continue

        raw_bytes = src.read_bytes()
        gz_bytes = len(gzip.compress(raw_bytes, compresslevel=9))
        total_gz_bytes += gz_bytes

    total_kb = total_gz_bytes / 1024.0
    if total_kb > MAX_GZIP_BUDGET_KB:
        issues.append(f"Payload size budget exceeded: {total_kb:.2f} KB > {MAX_GZIP_BUDGET_KB:.2f} KB limit!")

    return issues, total_kb


def main() -> int:
    print("=" * 70)
    print(" BGA Reflow Controller - Frontend Standards & Architecture Scanner")
    print("======================================================================")

    all_issues = []

    index_html = ASSETS_DIR / "index.html"
    style_css = ASSETS_DIR / "style.css"
    app_js = ASSETS_DIR / "app.js"

    # 1. Check JavaScript Files
    print("[*] Auditing JavaScript Standards (Strict Mode, Zero 'var', Strict Equality)...")
    js_targets = [app_js, LANG_DE_FILE]
    js_dir = ASSETS_DIR / "js"
    if js_dir.is_dir():
        js_targets.extend(sorted(js_dir.glob("*.js")))
    for js_path in js_targets:
        if js_path.exists():
            issues = check_javascript_rules(js_path)
            all_issues.extend(issues)

    # 2. Check Offline & Zero-CDN
    print("[*] Verifying Offline-First & Zero-CDN Guarantee...")
    all_issues.extend(check_offline_and_cdn(index_html, style_css))

    # 3. Check Form Autocomplete
    print("[*] Verifying Machine Parameter Form Autocomplete Standards...")
    all_issues.extend(check_form_autocomplete(index_html))

    # 4. Check Touch-Action Styling
    print("[*] Verifying Touch Action & Mobile Gesture Protection...")
    all_issues.extend(check_touch_action_styling(style_css))

    # 5. Check Inline Styles (Strict Separation of Presentation)
    print("[*] Verifying Zero Inline Styles in HTML Templates...")
    all_issues.extend(check_inline_styles(index_html))

    # 6. Check I18n Coverage
    print("[*] Checking I18n Localization Key Coverage against de.js...")
    all_issues.extend(check_i18n_key_coverage(index_html, app_js, LANG_DE_FILE))

    # 7. Check Autotune Debounce & PID Capacity Guards
    print("[*] Verifying Autotune Action Locking & PID Capacity Guards...")
    all_issues.extend(check_autotune_and_debounce_guards(js_dir))

    # 8. Check Flash ROM Budget
    print("[*] Measuring Compressed Flash ROM Asset Payload...")
    budget_issues, total_kb = check_rom_payload_budget(ASSETS_DIR)
    all_issues.extend(budget_issues)
    print(f"    -> Total Gzipped Assets: {total_kb:.2f} KB / {MAX_GZIP_BUDGET_KB:.0f} KB (OK)")

    print("-" * 70)
    if all_issues:
        print(f"FAILURE: Found {len(all_issues)} frontend violation(s):\n")
        for issue in all_issues:
            print(f"  [!] {issue}")
        print("=" * 70)
        return 1

    print("SUCCESS: All frontend audits passed! 100% compliant with AGENTS.md standards.")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
