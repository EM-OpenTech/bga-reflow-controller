#!/usr/bin/env python3
"""
SPDX-FileCopyrightText: 2026 EM-OpenTech
SPDX-License-Identifier: AGPL-3.0-or-later

tools/pack_web.py
Compresses web assets from 'assets/' directory into 'components/web/build_assets' using gzip (.gz).
Suitable for Zero-Copy Flash ROM embedding via CMake EMBED_FILES in StaticFileServer.
"""

import os
import gzip
import shutil
from pathlib import Path

# Directories
PROJECT_ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIR = PROJECT_ROOT / "assets"
EMBED_DIR = PROJECT_ROOT / "components" / "web" / "build_assets"

# Extensions to gzip compress
GZIP_EXTENSIONS = {".html", ".css", ".js", ".json", ".svg", ".txt"}

def pack_web_assets():
    if not SOURCE_DIR.exists():
        print(f"[ERROR] Source directory not found: {SOURCE_DIR}")
        return False

    # Clean embed target directory
    if EMBED_DIR.exists():
        shutil.rmtree(EMBED_DIR)
    EMBED_DIR.mkdir(parents=True, exist_ok=True)

    # Read version.txt if available
    version_file = PROJECT_ROOT / "version.txt"
    app_version = "unknown"
    if version_file.exists():
        app_version = version_file.read_text().strip()

    print(f"=== Compressing Web Assets for Firmware ROM Embedding (Version: {app_version}) ===")
    print(f"Source: {SOURCE_DIR}")
    print(f"Target: {EMBED_DIR}\n")

    total_orig_size = 0
    total_gz_size = 0
    file_count = 0

    for root, dirs, files in os.walk(SOURCE_DIR):
        # Skip READMEs or hidden files in assets
        for file in files:
            if file.startswith(".") or file.endswith(".md"):
                continue

            src_file = Path(root) / file
            rel_path = src_file.relative_to(SOURCE_DIR)
            ext = src_file.suffix.lower()
            
            orig_size = src_file.stat().st_size
            total_orig_size += orig_size
            file_count += 1

            if ext in GZIP_EXTENSIONS:
                dest_file = EMBED_DIR / f"{src_file.name}.gz"
                
                # For index.html, inject cache buster query string into asset references
                if src_file.name == "index.html":
                    content = src_file.read_text(encoding="utf-8")
                    content = content.replace('href="style.css"', f'href="style.css?v={app_version}"')
                    content = content.replace('src="chart.min.js"', f'src="chart.min.js?v={app_version}"')
                    content = content.replace('src="app.js"', f'src="app.js?v={app_version}"')
                    content_bytes = content.encode("utf-8")
                    with gzip.open(dest_file, "wb", compresslevel=9) as f_out:
                        f_out.write(content_bytes)
                else:
                    # Compress with maximum compression (level 9)
                    with open(src_file, "rb") as f_in:
                        with gzip.open(dest_file, "wb", compresslevel=9) as f_out:
                            shutil.copyfileobj(f_in, f_out)
                
                gz_size = dest_file.stat().st_size
                total_gz_size += gz_size
                ratio = (1.0 - (gz_size / orig_size)) * 100 if orig_size > 0 else 0
                print(f"  [GZ] {rel_path} -> {dest_file.name} ({orig_size} B -> {gz_size} B, -{ratio:.1f}%)")
            else:
                dest_file = EMBED_DIR / src_file.name
                shutil.copy2(src_file, dest_file)
                total_gz_size += orig_size
                print(f"  [CP] {rel_path} ({orig_size} B)")

    print(f"\n================================")
    print(f"Packed {file_count} files successfully.")
    print(f"Original size:    {total_orig_size / 1024:.2f} KB")
    print(f"Compressed size:  {total_gz_size / 1024:.2f} KB")
    if total_orig_size > 0:
        saved_pct = (1.0 - (total_gz_size / total_orig_size)) * 100
        print(f"Flash space saved: {saved_pct:.1f}%\n")
    return True

if __name__ == "__main__":
    pack_web_assets()
