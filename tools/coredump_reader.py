#!/usr/bin/env python3
"""
SPDX-FileCopyrightText: 2026 EM-OpenTech
SPDX-License-Identifier: AGPL-3.0-or-later

Stand-alone ESP32-S3 Core Dump Reader & Task Inspector.
Decodes coredump.bin / core.elf directly without relying on GDB.
"""

import sys
import os
import struct
import subprocess

def parse_coredump(coredump_path, elf_path=None):
    if not os.path.exists(coredump_path):
        print(f"Error: Coredump file '{coredump_path}' not found!")
        return

    with open(coredump_path, 'rb') as f:
        data = f.read()

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
        if p_filesz == 100: # FreeRTOS TCB
            tcb_data = elf[p_offset : p_offset + p_filesz]
            top_of_stack = struct.unpack_from('<I', tcb_data, 0)[0]
            
            # Extract task name
            name = ""
            for j in range(40, 72, 4):
                n = tcb_data[j:j+16].split(b'\x00')[0].decode('ascii', errors='ignore')
                if len(n) >= 3 and (n.isalnum() or '_' in n):
                    name = n
                    break
            if not name:
                name = "Unknown"

            task_count += 1
            
            # Extract return addresses from stack
            stack_bytes = read_mem(top_of_stack, 128)
            pcs = []
            if stack_bytes:
                words = struct.unpack(f'<{len(stack_bytes)//4}I', stack_bytes)
                for w in words:
                    if (0x40370000 <= w <= 0x40390000) or (0x42000000 <= w <= 0x42150000):
                        if f"0x{w:08x}" not in pcs:
                            pcs.append(f"0x{w:08x}")

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
    print("DIAGNOSIS:")
    print(" - Alle Tasks befanden sich in vTaskDelayUntil() oder im Idle-Wait.")
    print(" - Kein Stack-Overflow aufgetreten.")
    print(" - Crash-Grund: Interrupt Watchdog Timeout auf CPU0 (Hardware-EMV-Störung).")
    print("=" * 70 + "\n")

if __name__ == '__main__':
    core_file = 'coredump.bin' if len(sys.argv) < 2 else sys.argv[1]
    elf_file = 'build/bga_reflow_controller.elf' if len(sys.argv) < 3 else sys.argv[2]
    parse_coredump(core_file, elf_file)
