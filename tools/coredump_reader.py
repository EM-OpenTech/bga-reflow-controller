#!/usr/bin/env python3
# =============================================================================
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
# =============================================================================

"""
BGA Reflow Controller - Stand-alone ESP32-S3 Core Dump Reader & Task Inspector.
Decodes coredump.bin / core.elf directly without relying on GDB, and can optionally
download the coredump partition automatically from a connected ESP32-S3 via esptool.

Typische Befehle / Usage Examples:
----------------------------------
# 1. Automatisch vom ESP herunterladen und analysieren:
python3 tools/coredump_reader.py -f

# 2. Mit explizitem Port und schnellerer Baudrate:
python3 tools/coredump_reader.py -f -p /dev/cu.usbmodem1101 -b 460800

# 3. Reine Offline-Analyse einer bestehenden Datei (wie bisher):
python3 tools/coredump_reader.py coredump.bin
"""

import argparse
import glob
import os
import struct
import subprocess
import sys

# =============================================================================
# Hardware Communication & Partition Fetching
# =============================================================================

def find_default_port():
    """Detect candidate USB serial port for connected ESP32-S3."""
    candidates = (
        glob.glob('/dev/cu.usbmodem*')
        + glob.glob('/dev/cu.usbserial*')
        + glob.glob('/dev/cu.SLAB_USBtoUART*')
        + glob.glob('/dev/cu.wchusbserial*')
        + glob.glob('/dev/ttyACM*')
        + glob.glob('/dev/ttyUSB*')
    )
    if candidates:
        return candidates[0]
    return '/dev/cu.usbmodem*'


def fetch_coredump_from_esp(
    port=None,
    baud=115200,
    addr="0xff0000",
    size="0x10000",
    output_file="coredump.bin",
):
    """Download coredump partition from ESP32 flash using esptool.py."""
    if not port:
        port = find_default_port()

    print("\n" + "=" * 70)
    print("         DOWNLOADING COREDUMP FROM ESP32-S3 VIA ESPTOOL        ")
    print("=" * 70)
    print(f"[*] Target Port:   {port}")
    print(f"[*] Baud Rate:     {baud}")
    print(f"[*] Flash Region:  Offset {addr}, Size {size} (64 KB)")
    print(f"[*] Output File:   {output_file}")

    activate_script = os.path.expanduser("~/.espressif/tools/activate_idf_v6.0.2.sh")
    if os.path.exists(activate_script):
        cmd = (
            f". \"{activate_script}\" && esptool.py -p {port} -b {baud}"
            f" read_flash {addr} {size} \"{output_file}\""
        )
    else:
        cmd = f"esptool.py -p {port} -b {baud} read_flash {addr} {size} \"{output_file}\""

    print(f"[*] Executing command:\n    {cmd}\n")

    shell = os.environ.get("SHELL", "/bin/zsh")
    try:
        ret = subprocess.run([shell, "-c", cmd], check=False)
        if ret.returncode != 0:
            print(f"\n[!] Error: esptool.py failed with return code {ret.returncode}.")
            print("    Please check USB connection, port permissions, or put ESP32 in boot mode if needed.\n")
            return False
    except Exception as e:
        print(f"\n[!] Failed to execute esptool command: {e}\n")
        return False

    if not os.path.exists(output_file) or os.path.getsize(output_file) == 0:
        print(f"[!] Output file '{output_file}' was not created or is empty.")
        return False

    print(f"\n[+] Successfully read {os.path.getsize(output_file)} bytes to '{output_file}'.\n")
    return True

# =============================================================================
# Core Dump Parser & Analysis Engine
# =============================================================================

def parse_coredump(coredump_path, elf_path=None):
    if not os.path.exists(coredump_path):
        print(f"Error: Coredump file '{coredump_path}' not found!")
        print("\nTip: Run with --fetch (or -f) to automatically download it from your ESP32-S3:")
        print("     python3 tools/coredump_reader.py --fetch")
        return

    with open(coredump_path, 'rb') as f:
        data = f.read()

    if not data:
        print(f"Error: Coredump file '{coredump_path}' is empty (0 bytes)!")
        return

    # Check if data contains UART base64 core dump
    if b"CORE DUMP START" in data:
        import base64
        try:
            start_marker = b"CORE DUMP START"
            end_marker = b"CORE DUMP END"
            start_idx = data.find(start_marker)
            start_idx = data.find(b"\n", start_idx) + 1
            end_idx = data.find(end_marker, start_idx)
            b64_content = data[start_idx:end_idx].strip() if end_idx != -1 else data[start_idx:].strip()
            b64_content = b"".join(b64_content.split())
            data = base64.b64decode(b64_content)
        except Exception as e:
            print(f"Warning: Failed to decode base64 UART core dump: {e}")

    # Check if flash partition is blank / erased (all 0xFF)
    if all(b == 0xFF for b in data[:min(len(data), 1024)]) and all(b == 0xFF for b in data):
        print("\n" + "=" * 70)
        print(f"STATUS: '{coredump_path}' ({len(data)} bytes) consists entirely of 0xFF.")
        print("=" * 70)
        print("  -> Die Flash-Partition ist vollstaendig leer / unbeschrieben (0xFF).")
        print("  -> Es wurde bisher KEIN Coredump / Panic auf den Flash geschrieben!")
        print("\n  Hintergrund:")
        print("  1. Die Firmware ist stabil gelaufen und hat keinen Absturz (Panic) erlitten.")
        print("  2. Ein etwaiger Neustart erfolgte ohne Panic (z.B. Hardware-Reset, Brownout).")
        print("  3. Die Flash-Partition wurde nach dem letzten Crash neu geflasht/geloescht.")
        print("=" * 70 + "\n")
        return

    # Check if file consists entirely of 0x00
    if all(b == 0x00 for b in data[:min(len(data), 1024)]) and all(b == 0x00 for b in data):
        print(f"\nStatus: '{coredump_path}' ({len(data)} bytes) consists entirely of 0x00 (empty).")
        print("No core dump data present.\n")
        return

    # Check for ESP coredump container header (starts with total length)
    offset = 0
    if data[12:16] == b'\x7fELF':
        offset = 12
    elif data[0:4] == b'\x7fELF':
        offset = 0
    else:
        # Search for ELF magic
        idx = data.find(b'\x7fELF')
        if idx != -1:
            offset = idx
        else:
            print("Error: No valid ELF core dump signature found in file!")
            return

    elf = data[offset:]

    # Parse 32-bit ELF Header
    e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags, e_ehsize, e_phentsize, e_phnum = struct.unpack_from('<HHIIIIIHHH', elf, 16)
    
    segments = []
    for i in range(e_phnum):
        p_off = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from('<IIIIIIII', elf, p_off)
        segments.append((p_type, p_offset, p_vaddr, p_filesz))

    def read_mem(vaddr, size):
        for p_type, p_offset, p_vaddr, p_filesz in segments:
            if p_vaddr <= vaddr < p_vaddr + p_filesz:
                in_offset = vaddr - p_vaddr
                return elf[p_offset + in_offset : p_offset + in_offset + size]
        return None

    print("\n" + "=" * 70)
    print("           ESP32-S3 COREDUMP TASK & REGISTER REPORT           ")
    print("=" * 70)

    task_count = 0
    tasks_info = []

    for p_type, p_offset, p_vaddr, p_filesz in segments:
        # Match FreeRTOS TCBs across ESP-IDF versions (typically 100-400 bytes, e.g. 340 on ESP32-S3 SMP)
        if 80 <= p_filesz <= 512 and p_type == 1:
            tcb_data = elf[p_offset : p_offset + p_filesz]
            top_of_stack = struct.unpack_from('<I', tcb_data, 0)[0]

            # Extract task name from TCB (pcTaskName field at offset 52 on ESP32-S3 FreeRTOS)
            name = ""
            if len(tcb_data) > 68:
                raw_name = tcb_data[52 : 52 + 16].split(b'\x00')[0]
                if len(raw_name) >= 2 and all(32 <= b < 127 for b in raw_name):
                    name = raw_name.decode('ascii', errors='ignore')

            # Fallback search if layout differs
            if not name:
                for j in range(32, min(p_filesz - 16, 160)):
                    chunk = tcb_data[j : j + 16].split(b'\x00')[0]
                    if len(chunk) >= 3 and chunk[0:1].isalpha() and all(32 <= b < 127 for b in chunk):
                        name = chunk.decode('ascii', errors='ignore')
                        break
            if not name:
                continue

            task_count += 1

            # Find matching stack segment (the memory segment immediately preceding or containing top_of_stack)
            stack_bytes = None
            for s_type, s_offset, s_vaddr, s_filesz in segments:
                if s_vaddr <= top_of_stack <= s_vaddr + s_filesz:
                    stack_bytes = elf[s_offset : s_offset + s_filesz]
                    break
                # Also check adjacent stack allocation right before TCB
                if s_vaddr + s_filesz <= p_vaddr <= s_vaddr + s_filesz + 64:
                    stack_bytes = elf[s_offset : s_offset + s_filesz]
                    break

            pcs = []
            if stack_bytes:
                words = struct.unpack(f'<{len(stack_bytes) // 4}I', stack_bytes[: (len(stack_bytes) // 4) * 4])
                for w in words:
                    if (0x40370000 <= w <= 0x40390000) or (0x42000000 <= w <= 0x42150000):
                        pc_str = f"0x{w:08x}"
                        if pc_str not in pcs:
                            pcs.append(pc_str)

            tasks_info.append({
                'name': name,
                'tcb': p_vaddr,
                'stack_top': top_of_stack,
                'pcs': pcs[:4]
            })

    print(f"Total Active FreeRTOS Tasks Found: {task_count}\n")
    print(f"{'Task Name':<20} | {'TCB Addr':<12} | {'Stack Top':<12} | {'Recent Program Counters (PCs)'}")
    print("-" * 70)
    for t in tasks_info:
        pcs_str = ", ".join(t['pcs']) if t['pcs'] else "N/A"
        print(f"{t['name']:<20} | 0x{t['tcb']:08x}   | 0x{t['stack_top']:08x}   | {pcs_str}")

    print("\n" + "=" * 70)
    print("TASK SUMMARY:")
    print(f" - Analyzed {task_count} FreeRTOS task contexts from coredump.")
    print(" - Inspect recent Program Counters (PCs) above against your firmware ELF map.")
    if elf_path and os.path.exists(elf_path):
        print(f" - Matching firmware ELF: {elf_path}")
        print("   To view full backtraces with line numbers, run:")
        print(f"     espcoredump.py info_corefile -e {elf_path} {coredump_path}")
    print("=" * 70 + "\n")

# =============================================================================
# Main Entry Point & CLI Parser
# =============================================================================

def main():
    epilog_text = """Typische Befehle / Examples:
  # 1. Automatisch vom ESP herunterladen und analysieren:
  python3 tools/coredump_reader.py -f

  # 2. Mit explizitem Port und schnellerer Baudrate:
  python3 tools/coredump_reader.py -f -p /dev/cu.usbmodem1101 -b 460800

  # 3. Reine Offline-Analyse einer bestehenden Datei:
  python3 tools/coredump_reader.py coredump.bin
"""
    parser = argparse.ArgumentParser(
        description="BGA Reflow Controller - ESP32-S3 Core Dump Reader & Task Inspector",
        epilog=epilog_text,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        'coredump',
        nargs='?',
        default='coredump.bin',
        help="Path to coredump binary (default: coredump.bin)",
    )
    parser.add_argument(
        'elf',
        nargs='?',
        default='build/bga_reflow_controller.elf',
        help="Path to firmware ELF file (default: build/bga_reflow_controller.elf)",
    )
    parser.add_argument(
        '-f',
        '--fetch',
        action='store_true',
        help="Automatically download coredump partition from connected ESP32-S3 first",
    )
    parser.add_argument(
        '-p',
        '--port',
        default=None,
        help="Serial port for esptool (e.g. /dev/cu.usbmodem1101, default: auto-detect)",
    )
    parser.add_argument(
        '-b',
        '--baud',
        type=int,
        default=115200,
        help="Serial baud rate for reading flash (default: 115200)",
    )
    parser.add_argument(
        '--addr',
        default="0xff0000",
        help="Flash partition start address (default: 0xff0000)",
    )
    parser.add_argument(
        '--size',
        default="0x10000",
        help="Flash partition size in bytes (default: 0x10000 = 64KB)",
    )

    args = parser.parse_args()

    if args.fetch:
        success = fetch_coredump_from_esp(
            port=args.port,
            baud=args.baud,
            addr=args.addr,
            size=args.size,
            output_file=args.coredump,
        )
        if not success:
            sys.exit(1)

    parse_coredump(args.coredump, args.elf)


if __name__ == '__main__':
    main()
