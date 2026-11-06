#!/usr/bin/env python3
# ============================================================================
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
# ============================================================================

"""
BGA Reflow Controller - Web Assets Gzip Compression & Packager.
Bundles modular CSS/JS sources and compresses web assets from 'assets/' directory
into 'components/web/build_assets' using gzip (.gz).
Suitable for Zero-Copy Flash ROM embedding via CMake EMBED_FILES in StaticFileServer.
Uses atomic file replacement (.tmp -> rename) across all targets to guarantee zero
downtime, crash resilience, and complete race-condition safety with Ninja/CMake.
"""

import gzip
import os
import re
import shutil
from pathlib import Path

# Directories
PROJECT_ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIR = PROJECT_ROOT / "assets"
EMBED_DIR = PROJECT_ROOT / "components" / "web" / "build_assets"

# Extensions to gzip compress
GZIP_EXTENSIONS = {".html", ".css", ".js", ".json", ".svg", ".txt"}


def bundle_css_modules(source_dir: Path) -> None:
    """Concatenate modular CSS files from assets/css/*.css into assets/style.css atomically."""
    css_dir = source_dir / "css"
    if not css_dir.is_dir():
        return

    css_files = sorted(css_dir.glob("*.css"))
    if not css_files:
        return

    output_file = source_dir / "style.css"
    tmp_file = source_dir / "style.css.tmp"
    print(f"[INFO] Bundling {len(css_files)} CSS modules into {output_file.name} (atomic replace)...")

    bundled_parts = [
        "/*\n"
        " * SPDX-FileCopyrightText: 2026 EM-OpenTech\n"
        " * SPDX-License-Identifier: AGPL-3.0-or-later\n"
        " *\n"
        " * @file style.css\n"
        " * @brief Bundled production stylesheet compiled from assets/css/ modules.\n"
        " *\n"
        " * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later\n"
        " * @see https://github.com/EM-OpenTech/bga-reflow-controller\n"
        " */\n\n"
    ]

    for f in css_files:
        content = f.read_text(encoding="utf-8")
        # Strip duplicate license header from each module if present
        content = re.sub(r"/\*[\s\S]*?SPDX-FileCopyrightText[\s\S]*?\*/\s*", "", content)
        banner = (
            f"/* ============================================================\n"
            f"   Module: {f.name}\n"
            f"   ============================================================ */\n\n"
        )
        bundled_parts.append(banner + content.strip() + "\n\n")

    # 1. Write complete content to temporary file
    tmp_file.write_text("".join(bundled_parts), encoding="utf-8")
    # 2. Atomically swap destination via OS-level rename
    tmp_file.replace(output_file)
    print(f"[INFO] Successfully bundled {output_file.name} ({output_file.stat().st_size} bytes).\n")


def bundle_js_modules(source_dir: Path) -> None:
    """Concatenate modular JS files from assets/js/*.js into assets/app.js atomically."""
    js_dir = source_dir / "js"
    if not js_dir.is_dir():
        return

    js_files = sorted(js_dir.glob("*.js"))
    if not js_files:
        return

    output_file = source_dir / "app.js"
    tmp_file = source_dir / "app.js.tmp"
    print(f"[INFO] Bundling {len(js_files)} JS modules into {output_file.name} (atomic replace)...")

    bundled_parts = [
        "/*\n"
        " * SPDX-FileCopyrightText: 2026 EM-OpenTech\n"
        " * SPDX-License-Identifier: AGPL-3.0-or-later\n"
        " *\n"
        " * @file app.js\n"
        " * @brief Bundled production application script compiled from assets/js/ modules.\n"
        " *\n"
        " * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later\n"
        " * @see https://github.com/EM-OpenTech/bga-reflow-controller\n"
        " */\n\n",
        "(function () {\n"
        "\"use strict\";\n\n"
    ]

    for f in js_files:
        content = f.read_text(encoding="utf-8")
        content = re.sub(r"/\*[\s\S]*?SPDX-FileCopyrightText[\s\S]*?\*/\s*", "", content)
        banner = (
            f"// ============================================================\n"
            f"// Module: {f.name}\n"
            f"// ============================================================\n\n"
        )
        bundled_parts.append(banner + content.strip() + "\n\n")

    bundled_parts.append("})();\n")

    # 1. Write complete content to temporary file
    tmp_file.write_text("".join(bundled_parts), encoding="utf-8")
    # 2. Atomically swap destination via OS-level rename
    tmp_file.replace(output_file)
    print(f"[INFO] Successfully bundled {output_file.name} ({output_file.stat().st_size} bytes).\n")


def pack_web_assets():
    if not SOURCE_DIR.exists():
        print(f"[ERROR] Source directory not found: {SOURCE_DIR}")
        return False

    # 1. Atomically bundle modular CSS and JS before packaging
    bundle_css_modules(SOURCE_DIR)
    bundle_js_modules(SOURCE_DIR)

    # 2. Ensure target embed directory exists
    EMBED_DIR.mkdir(parents=True, exist_ok=True)

    # Read version.txt if available
    version_file = PROJECT_ROOT / "version.txt"
    app_version = "unknown"
    if version_file.exists():
        app_version = version_file.read_text().strip()

    print(f"=== Compressing Web Assets for Firmware ROM Embedding (Version: {app_version}) ===")
    print(f"Source: {SOURCE_DIR}")
    print(f"Target: {EMBED_DIR} (Atomic replace active)\n")

    total_orig_size = 0
    total_gz_size = 0
    file_count = 0
    generated_files = set()

    for root, dirs, files in os.walk(SOURCE_DIR):
        # Exclude development module folders from the embedded ROM bundle
        if "css" in dirs:
            dirs.remove("css")
        if "js" in dirs:
            dirs.remove("js")

        # Skip READMEs, hidden files, or temporary files in assets
        for file in files:
            if file.startswith(".") or file.endswith(".md") or file.endswith(".tmp"):
                continue

            src_file = Path(root) / file
            rel_path = src_file.relative_to(SOURCE_DIR)
            ext = src_file.suffix.lower()

            orig_size = src_file.stat().st_size
            total_orig_size += orig_size
            file_count += 1

            if ext in GZIP_EXTENSIONS:
                dest_file = EMBED_DIR / f"{src_file.name}.gz"
                tmp_dest_file = EMBED_DIR / f"{src_file.name}.gz.tmp"

                # For index.html, inject cache buster query string into asset references
                if src_file.name == "index.html":
                    raw_src = src_file.read_text(encoding="utf-8")
                    content = re.sub(r'href="style\.css(\?v=[^"]*)?"', f'href="style.css?v={app_version}"', raw_src)
                    content = re.sub(r'src="chart\.min\.js(\?v=[^"]*)?"', f'src="chart.min.js?v={app_version}"', content)
                    content = re.sub(r'src="app\.js(\?v=[^"]*)?"', f'src="app.js?v={app_version}"', content)
                    # Automatically keep source index.html on disk synchronized with version.txt atomically
                    if content != raw_src:
                        tmp_html = src_file.parent / f"{src_file.name}.tmp"
                        tmp_html.write_text(content, encoding="utf-8")
                        tmp_html.replace(src_file)
                    content_bytes = content.encode("utf-8")
                    with gzip.open(tmp_dest_file, "wb", compresslevel=9) as f_out:
                        f_out.write(content_bytes)
                elif src_file.name == "app.js":
                    # For app.js, inject cache buster query string into dynamic language bundle load
                    content = src_file.read_text(encoding="utf-8")
                    content = content.replace("`/lang/${lang}.js`", f"`/lang/${{lang}}.js?v={app_version}`")
                    content_bytes = content.encode("utf-8")
                    with gzip.open(tmp_dest_file, "wb", compresslevel=9) as f_out:
                        f_out.write(content_bytes)
                else:
                    # Compress with maximum compression (level 9) into temporary file
                    with open(src_file, "rb") as f_in:
                        with gzip.open(tmp_dest_file, "wb", compresslevel=9) as f_out:
                            shutil.copyfileobj(f_in, f_out)

                # Atomically swap temporary compressed file into place
                tmp_dest_file.replace(dest_file)
                generated_files.add(dest_file.name)

                gz_size = dest_file.stat().st_size
                total_gz_size += gz_size
                ratio = (1.0 - (gz_size / orig_size)) * 100 if orig_size > 0 else 0
                print(f"  [GZ] {rel_path} -> {dest_file.name} ({orig_size} B -> {gz_size} B, -{ratio:.1f}%)")
            else:
                dest_file = EMBED_DIR / src_file.name
                tmp_dest_file = EMBED_DIR / f"{src_file.name}.tmp"
                shutil.copy2(src_file, tmp_dest_file)
                tmp_dest_file.replace(dest_file)
                generated_files.add(dest_file.name)

                total_gz_size += orig_size
                print(f"  [CP] {rel_path} ({orig_size} B)")

    # 3. Clean up stale/obsolete files in target directory that were not generated
    stale_count = 0
    for item in EMBED_DIR.iterdir():
        if item.is_file() and item.name not in generated_files and not item.name.startswith("."):
            item.unlink()
            stale_count += 1
    if stale_count > 0:
        print(f"\n[INFO] Cleaned {stale_count} stale file(s) from target folder.")

    print("\n================================")
    print(f"Packed {file_count} files successfully (100% atomic replace).")
    print(f"Original size:    {total_orig_size / 1024:.2f} KB")
    print(f"Compressed size:  {total_gz_size / 1024:.2f} KB")
    if total_orig_size > 0:
        saved_pct = (1.0 - (total_gz_size / total_orig_size)) * 100
        print(f"Flash space saved: {saved_pct:.1f}%\n")
    return True


if __name__ == "__main__":
    pack_web_assets()
