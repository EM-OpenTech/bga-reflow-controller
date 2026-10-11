#!/usr/bin/env python3
"""
SPDX-FileCopyrightText: 2026 EM-OpenTech
SPDX-License-Identifier: AGPL-3.0-or-later

tools/build_factory_bin.py
Generates release binaries for ESP32-S3:
  1. firmware-v<version>.bin: Monolithic 16 MB factory image for USB / Web-Flasher @ Offset 0x0
  2. ota-update-v<version>.bin: Compact app binary (~2 MB) for Web-UI OTA updates / downgrades

Version is dynamically read from version.txt.
Overwrites and cleans up previous release binaries before generation.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def get_version(project_dir: Path) -> str:
    version_file = project_dir / "version.txt"
    if not version_file.exists():
        print(f"[WARN] version.txt not found at {version_file}, using fallback 'unknown'")
        return "unknown"
    return version_file.read_text(encoding="utf-8").strip()


def find_esptool() -> list[str]:
    """Find a working esptool invocation command."""
    # 1. Try python module import in current python interpreter
    try:
        import esptool  # noqa: F401
        return [sys.executable, "-m", "esptool"]
    except ImportError:
        pass

    # 2. Try standalone executable in PATH
    esptool_bin = shutil.which("esptool")
    if esptool_bin:
        return [esptool_bin]

    esptool_py_bin = shutil.which("esptool.py")
    if esptool_py_bin:
        return [esptool_py_bin]

    # Fallback to sys.executable -m esptool
    return [sys.executable, "-m", "esptool"]


def build_factory_image(project_dir: Path, build_dir: Path) -> bool:
    version_str = get_version(project_dir)
    # Ensure 'v' prefix: 1.0.0-beta.2 -> v1.0.0-beta.2
    version_tag = version_str if version_str.startswith("v") else f"v{version_str}"
    
    factory_filename = f"firmware-{version_tag}.bin"
    factory_path = build_dir / factory_filename
    
    ota_filename = f"ota-update-{version_tag}.bin"
    ota_path = build_dir / ota_filename

    print(f"\n=== Generating Release Images (Version: {version_tag}) ===")
    print(f"Project directory:  {project_dir}")
    print(f"Build directory:    {build_dir}")
    print(f"Factory Full Image: {factory_filename}")
    print(f"OTA Update Image:   {ota_filename}\n")

    if not build_dir.exists():
        print(f"[ERROR] Build directory does not exist: {build_dir}")
        print("[ERROR] Please run 'idf.py build' first.")
        return False

    # 1. Clean up previous firmware and OTA binaries in build_dir
    patterns_to_clean = ["firmware*.bin", "ota-update-*.bin", "factory_full_image.bin"]
    cleaned_count = 0
    for pattern in patterns_to_clean:
        for old_file in build_dir.glob(pattern):
            try:
                old_file.unlink()
                print(f"[INFO] Removed previous binary: {old_file.name}")
                cleaned_count += 1
            except OSError as err:
                print(f"[WARN] Could not remove {old_file.name}: {err}")

    if cleaned_count == 0:
        print("[INFO] No existing release binaries found to remove.")

    # 2. Check required input binaries
    app_bin = build_dir / "bga_reflow_controller.bin"
    partitions = [
        (0x0, build_dir / "bootloader" / "bootloader.bin", "Bootloader"),
        (0x8000, build_dir / "partition_table" / "partition-table.bin", "Partition Table"),
        (0xE000, build_dir / "ota_data_initial.bin", "OTA Initial Data"),
        (0x10000, app_bin, "App Firmware"),
    ]

    missing = []
    for offset, file_path, desc in partitions:
        if not file_path.exists():
            missing.append(f"{desc} ({file_path})")

    if missing:
        print("\n[ERROR] Missing required build artifacts:")
        for item in missing:
            print(f"  - {item}")
        print("\n[ERROR] Please run 'idf.py build' before creating release images.")
        return False

    # 3. Assemble and execute esptool merge-bin for the monolithic 16 MB factory image
    esptool_cmd = find_esptool()

    merge_args = [
        "--chip", "esp32s3",
        "merge-bin",
        "-o", str(factory_path),
        "--pad-to-size", "16MB",
        "--flash-mode", "dio",
        "--flash-freq", "80m",
        "--flash-size", "16MB",
    ]

    for offset, file_path, _ in partitions:
        merge_args.extend([hex(offset), str(file_path)])

    full_cmd = esptool_cmd + merge_args
    print("\n[EXEC] Running esptool merge-bin for factory full image...")
    try:
        subprocess.run(full_cmd, check=True)
    except subprocess.CalledProcessError as e:
        print(f"[ERROR] esptool merge-bin failed with exit code {e.returncode}")
        return False
    except FileNotFoundError as e:
        print(f"[ERROR] Could not execute esptool: {e}")
        return False

    if not factory_path.exists():
        print(f"[ERROR] Expected factory image was not created: {factory_path}")
        return False

    # 4. Generate the OTA update image (copy of app firmware binary)
    try:
        shutil.copy2(app_bin, ota_path)
    except OSError as e:
        print(f"[ERROR] Failed to create OTA update binary: {e}")
        return False

    size_factory_mb = factory_path.stat().st_size / (1024 * 1024)
    size_ota_mb = ota_path.stat().st_size / (1024 * 1024)

    print("\n========================================================")
    print(" SUCCESS: Release Binaries Ready for GitHub / Deployment!")
    print(f" 1. Factory Full Image: {factory_path.name} ({size_factory_mb:.1f} MB)")
    print("    Flash Offset:       0x0 (Web Flasher, esptool, Initial Setup)")
    print(f" 2. OTA Update Image:   {ota_path.name} ({size_ota_mb:.2f} MB)")
    print("    Flash Offset:       None (Web-UI Browser Update / Downgrade)")
    print("========================================================\n")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Generate release images: firmware-v<ver>.bin and ota-update-v<ver>.bin"
    )
    default_root = Path(__file__).resolve().parent.parent
    parser.add_argument(
        "--project-dir",
        type=Path,
        default=default_root,
        help="Path to project root directory containing version.txt",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=default_root / "build",
        help="Path to build directory containing compiled .bin files",
    )

    args = parser.parse_args()
    success = build_factory_image(args.project_dir, args.build_dir)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
