#!/usr/bin/env python3
# =============================================================================
# SPDX-FileCopyrightText: 2026 EM-OpenTech
# SPDX-License-Identifier: AGPL-3.0-or-later
# =============================================================================

"""
BGA Reflow Controller - Stand-alone ESP32-S3 Core Dump Reader & Task Inspector.
Decodes coredump.bin / core.elf directly without relying on GDB, decodes panic reasons,
resolves C++ function symbols from ELF, and can automatically download/erase the coredump
partition from a connected ESP32-S3 via esptool.

Typische Befehle / Usage Examples:
----------------------------------
# 1. Automatisch vom ESP herunterladen und analysieren:
python3 tools/coredump_reader.py -f

# 2. Mit explizitem Port und schnellerer Baudrate:
python3 tools/coredump_reader.py -f -p /dev/cu.usbmodem1101 -b 460800

# 3. Reine Offline-Analyse einer bestehenden Datei (inkl. Symbolaufloesung):
python3 tools/coredump_reader.py coredump.bin build/bga_reflow_controller.elf

# 4. Coredump-Flash-Partition auf dem ESP32 loeschen:
python3 tools/coredump_reader.py --erase
"""

import argparse
import glob
import os
import struct
import subprocess
import sys

# =============================================================================
# Xtensa Architecture Exception Causes & Descriptions
# =============================================================================

XTENSA_EXCCAUSE_MAP = {
    0: "IllegalInstruction (Illegal instruction executed)",
    1: "Syscall (System call instruction)",
    2: "InstructionFetchError (Processor fetch error from memory)",
    3: "LoadStoreError (Memory load/store access error / bus fault)",
    4: "Level1Interrupt (Level-1 interrupt)",
    5: "Alloca (Alloca instruction/stack allocation error)",
    6: "IntegerDivideByZero (Integer division by zero)",
    9: "LoadStoreAlignment (Unaligned memory load/store)",
    12: "InstrPIFDataError (PIF data error during instruction fetch)",
    13: "LoadStorePIFDataError (PIF data error during load/store)",
    14: "InstrPIFAddrError (PIF address error during instruction fetch)",
    15: "LoadStorePIFAddrError (PIF address error during load/store)",
    28: "LoadProhibited (Read from non-readable or unmapped memory)",
    29: "StoreProhibited (Write to read-only or unmapped memory)",
    64: "Unhandled debug exception",
    65: "Double exception",
    66: "Unhandled kernel exception",
    67: "Coprocessor exception",
    68: "Interrupt wdt timeout on CPU0 (Interrupt Watchdog Timer)",
    69: "Interrupt wdt timeout on CPU1 (Interrupt Watchdog Timer)",
    70: "Cache error (Access to cached region with cache disabled / bus fault)",
}

# =============================================================================
# ELF Symbol Loader & Resolver
# =============================================================================

def load_elf_symbols(elf_path):
    """Load and demangle symbols from firmware ELF using nm."""
    symbols = []
    if not elf_path or not os.path.exists(elf_path):
        return symbols

    try:
        res = subprocess.run(
            ['nm', '-n', '-C', elf_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        for line in res.stdout.splitlines():
            parts = line.strip().split(maxsplit=2)
            if len(parts) >= 3:
                try:
                    addr = int(parts[0], 16)
                    symbols.append((addr, parts[2]))
                except ValueError:
                    pass
        symbols.sort(key=lambda x: x[0])
    except Exception:
        pass
    return symbols


def lookup_symbol(symbols, pc_val):
    """Binary-search symbol table for given program counter address."""
    if not symbols:
        return ""
    low = 0
    high = len(symbols) - 1
    best = None
    while low <= high:
        mid = (low + high) // 2
        if symbols[mid][0] <= pc_val:
            best = symbols[mid]
            low = mid + 1
        else:
            high = mid - 1
    if best:
        offset = pc_val - best[0]
        if offset == 0:
            return best[1]
        return f"{best[1]} (+0x{offset:x})"
    return ""

# =============================================================================
# Hardware Communication & Partition Fetching / Erasing
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


def erase_coredump_partition(
    port=None,
    baud=115200,
    addr="0xff0000",
    size="0x10000",
):
    """Erase coredump flash partition on ESP32 using esptool.py."""
    if not port:
        port = find_default_port()

    print("\n" + "=" * 70)
    print("         ERASING COREDUMP FLASH PARTITION ON ESP32-S3          ")
    print("=" * 70)
    print(f"[*] Target Port:   {port}")
    print(f"[*] Baud Rate:     {baud}")
    print(f"[*] Flash Region:  Offset {addr}, Size {size} (64 KB)")

    activate_script = os.path.expanduser("~/.espressif/tools/activate_idf_v6.0.2.sh")
    if os.path.exists(activate_script):
        cmd = (
            f". \"{activate_script}\" && esptool.py -p {port} -b {baud}"
            f" erase_region {addr} {size}"
        )
    else:
        cmd = f"esptool.py -p {port} -b {baud} erase_region {addr} {size}"

    print(f"[*] Executing command:\n    {cmd}\n")

    shell = os.environ.get("SHELL", "/bin/zsh")
    try:
        ret = subprocess.run([shell, "-c", cmd], check=False)
        if ret.returncode != 0:
            print(f"\n[!] Error: esptool.py failed with return code {ret.returncode}.")
            return False
    except Exception as e:
        print(f"\n[!] Failed to execute esptool command: {e}\n")
        return False

    print("\n[+] Successfully erased coredump flash partition!\n")
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
    notes = {}

    for i in range(e_phnum):
        p_off = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from('<IIIIIIII', elf, p_off)
        segments.append((p_type, p_offset, p_vaddr, p_filesz))

        # Extract PT_NOTE sections
        if p_type == 4:
            note_data = elf[p_offset : p_offset + p_filesz]
            pos = 0
            while pos + 12 <= len(note_data):
                namesz, descsz, n_type = struct.unpack_from('<3I', note_data, pos)
                pos += 12
                name = note_data[pos : pos + namesz]
                pos = (pos + namesz + 3) & ~3
                desc = note_data[pos : pos + descsz]
                pos = (pos + descsz + 3) & ~3
                notes[n_type] = (name, desc)

    # Load ELF symbols for function resolution
    symbols = load_elf_symbols(elf_path)

    # -------------------------------------------------------------------------
    # Parse Crash & Panic Metadata from Notes
    # -------------------------------------------------------------------------
    crashed_tcb = None
    panic_reason_str = "Unknown"
    exccause_val = None
    excvaddr_val = None
    epc1_val = None
    epc4_val = None

    if 677 in notes:  # ESP_EXTRA_INFO note
        desc = notes[677][1]
        if len(desc) >= 4:
            crashed_tcb = struct.unpack_from('<I', desc, 0)[0]
            regs = {}
            idx = 4
            while idx + 8 <= len(desc):
                tag, val = struct.unpack_from('<II', desc, idx)
                regs[tag] = val
                idx += 8

            exccause_val = regs.get(0xe8)
            excvaddr_val = regs.get(0xee)
            epc1_val = regs.get(0xb1)
            epc4_val = regs.get(0xb4)

            if exccause_val is not None:
                panic_reason_str = XTENSA_EXCCAUSE_MAP.get(exccause_val, f"Unknown Cause (0x{exccause_val:x})")

    app_sha256 = ""
    if 8266 in notes:  # ESP_CORE_DUMP_INFO note
        desc = notes[8266][1]
        if len(desc) >= 20:
            raw_sha = desc[4:36].split(b'\x00')[0]
            app_sha256 = raw_sha.decode('ascii', errors='ignore')

    # -------------------------------------------------------------------------
    # Task Extraction
    # -------------------------------------------------------------------------
    task_count = 0
    tasks_info = []

    for p_type, p_offset, p_vaddr, p_filesz in segments:
        if 80 <= p_filesz <= 512 and p_type == 1:
            tcb_data = elf[p_offset : p_offset + p_filesz]
            top_of_stack = struct.unpack_from('<I', tcb_data, 0)[0]

            name = ""
            if len(tcb_data) > 68:
                raw_name = tcb_data[52 : 52 + 16].split(b'\x00')[0]
                if len(raw_name) >= 2 and all(32 <= b < 127 for b in raw_name):
                    name = raw_name.decode('ascii', errors='ignore')

            if not name:
                for j in range(32, min(p_filesz - 16, 160)):
                    chunk = tcb_data[j : j + 16].split(b'\x00')[0]
                    if len(chunk) >= 3 and chunk[0:1].isalpha() and all(32 <= b < 127 for b in chunk):
                        name = chunk.decode('ascii', errors='ignore')
                        break
            if not name:
                continue

            task_count += 1

            stack_bytes = None
            for s_type, s_offset, s_vaddr, s_filesz in segments:
                if s_vaddr <= top_of_stack <= s_vaddr + s_filesz:
                    stack_bytes = elf[s_offset : s_offset + s_filesz]
                    break
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
                'pcs': pcs[:4],
                'is_crashed': (p_vaddr == crashed_tcb),
            })

    # Identify crashed task name
    crashed_task_name = "Unknown"
    for t in tasks_info:
        if t['is_crashed']:
            crashed_task_name = t['name']
            break

    # -------------------------------------------------------------------------
    # Display Panic Summary Banner
    # -------------------------------------------------------------------------
    RED = "\033[1;31m"
    GREEN = "\033[1;32m"
    YELLOW = "\033[1;33m"
    CYAN = "\033[1;36m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    RESET = "\033[0m"

    print(f"\n{BOLD}{'=' * 70}{RESET}")
    print(f"{CYAN}           ESP32-S3 COREDUMP TASK & REGISTER REPORT           {RESET}")
    print(f"{BOLD}{'=' * 70}{RESET}")

    crashed_color = RED if crashed_tcb else GREEN
    print(f"{YELLOW}CRASH & PANIC SUMMARY:{RESET}")
    print(f"  * Crashed Task:     {crashed_color}{crashed_task_name}{RESET} (TCB: 0x{crashed_tcb:08x})" if crashed_tcb else "  * Crashed Task:     None")
    print(f"  * Panic Reason:     {YELLOW}{panic_reason_str}{RESET}")
    if excvaddr_val is not None:
        print(f"  * Fault Address:    0x{excvaddr_val:08x} (EXCVADDR)")
    if epc1_val:
        sym = lookup_symbol(symbols, epc1_val)
        sym_str = f" -> {BOLD}{sym}{RESET}" if sym else ""
        print(f"  * Exception PC1:    0x{epc1_val:08x}{sym_str}")
    if epc4_val:
        sym = lookup_symbol(symbols, epc4_val)
        sym_str = f" -> {BOLD}{sym}{RESET}" if sym else ""
        print(f"  * Exception PC4:    0x{epc4_val:08x}{sym_str}")
    if app_sha256:
        print(f"  * Firmware SHA256:  {app_sha256}")
    print(f"{DIM}{'-' * 70}{RESET}\n")

    # -------------------------------------------------------------------------
    # Display Task Details (Optimized for VS Code Terminal Readability)
    # -------------------------------------------------------------------------
    print(f"{BOLD}ACTIVE FREERTOS TASKS ({task_count} Total):{RESET}\n")
    for idx, t in enumerate(tasks_info, 1):
        if t['is_crashed']:
            prefix = f"{RED}[CRASHED]{RESET}"
            name_str = f"{RED}{BOLD}{t['name']:<18}{RESET}"
        else:
            prefix = f"{GREEN}[ACTIVE] {RESET}"
            name_str = f"{CYAN}{t['name']:<18}{RESET}"

        print(f"{prefix} #{idx:02d} {name_str}  (TCB: 0x{t['tcb']:08x} | Stack Top: 0x{t['stack_top']:08x})")
        if t['pcs']:
            for pc_idx, p in enumerate(t['pcs'], 1):
                p_int = int(p, 16)
                sym = lookup_symbol(symbols, p_int)
                sym_str = f" -> {DIM}{sym}{RESET}" if sym else ""
                print(f"        └─ [{pc_idx}] {p}{sym_str}")
        else:
            print(f"        └─ (No recent PCs in stack frame)")
        print()

    print(f"{BOLD}{'=' * 70}{RESET}")
    print(f"{CYAN}TASK SUMMARY:{RESET}")
    print(f" - Analyzed {task_count} FreeRTOS task contexts from coredump.")
    if symbols:
        print(f" - Successfully resolved C++ symbols using {elf_path} ({len(symbols)} symbols loaded).")
    else:
        print(" - Tip: Pass path to firmware ELF file to automatically resolve function names.")
    print(f"{BOLD}{'=' * 70}{RESET}\n")

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

  # 4. Coredump-Partition auf dem ESP32 loeschen:
  python3 tools/coredump_reader.py --erase
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
        '--erase',
        action='store_true',
        help="Erase the coredump flash partition on the connected ESP32-S3",
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

    if args.erase:
        success = erase_coredump_partition(
            port=args.port,
            baud=args.baud,
            addr=args.addr,
            size=args.size,
        )
        if not success:
            sys.exit(1)
        return

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
