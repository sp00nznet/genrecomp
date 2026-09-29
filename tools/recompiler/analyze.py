#!/usr/bin/env python3
"""
genrecomp 68K analyzer: finds the code in a Genesis ROM and splits it into
functions for generate.py.

Recursive descent from the vector table (reset, interrupts, TRAPs) and the
title's seeds, then heuristic scans (address tables, address loads, function
prologues) over whatever descent did not decode. See docs/recompiler.md.

Standalone, it prints statistics or a disassembly for debugging. Anything it
writes is derived from the ROM: keep it out of every repo.

Usage:
    python analyze.py <rom> [--config recomp.json] [--stats] [--disasm]
"""

import re
import struct
import json
import sys
import os
from collections import defaultdict
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000

# ============================================================
# ROM parsing
# ============================================================

class GenesisROM:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        self.size = len(self.data)
        self._parse_header()
        self._parse_vectors()

    def _parse_header(self):
        self.console = self.data[0x100:0x110].decode('ascii', errors='replace').strip()
        self.copyright = self.data[0x110:0x120].decode('ascii', errors='replace').strip()
        self.title_domestic = self.data[0x120:0x150].decode('ascii', errors='replace').strip()
        self.title_overseas = self.data[0x150:0x180].decode('ascii', errors='replace').strip()
        self.serial = self.data[0x180:0x18E].decode('ascii', errors='replace').strip()
        self.checksum = struct.unpack('>H', self.data[0x18E:0x190])[0]
        self.rom_start = struct.unpack('>I', self.data[0x1A0:0x1A4])[0]
        self.rom_end = struct.unpack('>I', self.data[0x1A4:0x1A8])[0]
        self.region = self.data[0x1F0:0x1F3].decode('ascii', errors='replace').strip()

    def _parse_vectors(self):
        self.initial_sp = struct.unpack('>I', self.data[0x00:0x04])[0]
        self.initial_pc = struct.unpack('>I', self.data[0x04:0x08])[0]
        self.vectors = {}
        vector_names = {
            2: 'bus_error', 3: 'address_error', 4: 'illegal_insn',
            5: 'div_zero', 6: 'chk', 7: 'trapv',
            8: 'privilege_violation', 9: 'trace',
            25: 'irq1', 26: 'irq2', 27: 'irq3',
            28: 'irq4', 29: 'irq5', 30: 'irq6_vblank', 31: 'irq7',
        }
        vector_names.update({32 + n: f'trap{n}' for n in range(16)})
        for idx, name in vector_names.items():
            addr = struct.unpack('>I', self.data[idx*4:(idx+1)*4])[0]
            if addr != 0 and addr < self.size:
                self.vectors[name] = addr

    def read8(self, addr):
        if addr < self.size:
            return self.data[addr]
        return 0

    def read16(self, addr):
        if addr + 1 < self.size:
            return struct.unpack('>H', self.data[addr:addr+2])[0]
        return 0

    def read32(self, addr):
        if addr + 3 < self.size:
            return struct.unpack('>I', self.data[addr:addr+4])[0]
        return 0

    def print_info(self):
        print(f"Title:     {self.title_domestic}")
        print(f"Copyright: {self.copyright}")
        print(f"Serial:    {self.serial}")
        print(f"ROM size:  {self.size} bytes ({self.size//1024} KB)")
        print(f"Entry PC:  ${self.initial_pc:06X}")
        print(f"Initial SP:${self.initial_sp:06X}")
        print(f"Checksum:  ${self.checksum:04X}")
        print(f"Region:    {self.region}")


# ============================================================
# Recursive descent disassembler + function finder
# ============================================================

class M68KAnalyzer:
    CALL_MNEMONICS = {'bsr', 'jsr'}
    UNCONDITIONAL_ENDS = {'bra', 'jmp', 'rts', 'rte', 'rtr'}
    BIT_MNEMONICS = {'btst', 'bset', 'bclr', 'bchg'}

    def __init__(self, rom):
        self.rom = rom
        self.cs = Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
        self.cs.detail = True

        self.visited = set()
        self.instructions = {}          # addr -> (mnemonic, op_str, size, bytes)
        self.functions = {}             # addr -> dict
        self.call_graph = defaultdict(set)
        self.xrefs_to = defaultdict(set)
        self.labels = {}
        self.jump_tables = {}           # addr -> list of target addrs
        self.pc_switches = {}           # JMP addr -> case targets (see _pc_switch)

    # Per-title settings; see load_config(). Seeds are entry points descent
    # can't reach (targets of computed jumps, pointers stored in data).
    # Code ranges bound where the heuristic scans look.
    EXPLICIT_SEEDS = {}
    CODE_RANGES = [(0x200, 0x1000000)]

    def load_config(self, cfg):
        self.EXPLICIT_SEEDS = {int(k, 16): v for k, v in cfg.get('seeds', {}).items()}
        # Dispatch tables the scans can't recognise: count entries of 'size'
        # bytes (2 or 4, signed) at 'table', each an offset from 'base'
        # (0 for absolute addresses). Every target becomes an entry point.
        for t in cfg.get('offset_tables', []):
            table, base = int(t['table'], 16), int(t.get('base', '0'), 16)
            size, fmt = int(t.get('size', 4)), {2: '>h', 4: '>i'}[int(t.get('size', 4))]
            for i in range(int(t['count'])):
                off = struct.unpack(fmt, self.rom.data[table + size * i: table + size * (i + 1)])[0]
                tgt = (base + off) & 0xFFFFFF
                self.EXPLICIT_SEEDS.setdefault(tgt, f'sub_{tgt:06X}')
        if 'code_ranges' in cfg:
            self.CODE_RANGES = [(int(lo, 16), int(hi, 16)) for lo, hi in cfg['code_ranges']]

    def analyze(self):
        """Find code: recursive descent from the vectors and seeds first, then
        heuristic scans (address tables, address loads, prologues) over the
        bytes descent did not decode.

        Descent is certain; the scans are guesses. Running the guesses first
        let a longword inside an instruction's operand seed a "function" in
        the middle of that instruction, splitting real code (General Chaos,
        $000330). So a guessed target must be an instruction start or lie in
        undecoded bytes.
        """
        entry_points = {self.rom.initial_pc}
        for name, addr in self.rom.vectors.items():
            entry_points.add(addr)
            self.labels.setdefault(addr, f"vec_{name}")
        self.labels[self.rom.initial_pc] = "entry_point"
        for addr, name in self.EXPLICIT_SEEDS.items():
            entry_points.add(addr)
            self.labels[addr] = name
        print(f"{len(entry_points)} entry points from vectors and seeds")

        all_func_entries = set(entry_points)
        self._descend(entry_points, all_func_entries)

        sources = [entry_points]
        for scan in (self._scan_jump_tables, self._scan_address_loads, self._scan_prologues):
            guesses = {t for t in scan() if self._plausible(t)}
            print(f"  {scan.__name__}: {len(guesses)} plausible targets")
            all_func_entries.update(guesses)
            self._descend(guesses, all_func_entries)
            sources.append(guesses)

        dropped = self._resolve_overlaps(sources)
        all_func_entries -= dropped
        self._build_functions(all_func_entries)
        print(f"Disassembled {len(self.instructions)} instructions")
        print(f"Found {len(self.functions)} functions")
        print(f"Found {len(self.xrefs_to)} cross-references")

    def _resolve_overlaps(self, sources):
        """Drop instructions that overlap better-founded ones.

        Two decoded instructions that overlap can't both be code. Each gets
        the rank of the best source that reaches it through control flow:
        0 for vectors and seeds, then one per heuristic scan. The worse one
        goes, with any function entry on it. (General Chaos: an address
        table "entry" at $001870, inside a MULS at $00186E, turned the
        multiply into garbage and a loop count into a stack overrun.)
        """
        succ = {}
        for tgt, froms in self.xrefs_to.items():
            for f in froms:
                succ.setdefault(f, []).append(tgt)
        for a, cases in self.pc_switches.items():
            succ.setdefault(a, []).extend(cases)
        ENDS = ('bra', 'jmp', 'rts', 'rte', 'rtr')
        rank = {}
        for r, starts in enumerate(sources):
            work = [a for a in starts if a in self.instructions and a not in rank]
            for a in work:
                rank[a] = r
            while work:
                a = work.pop()
                mnem, _, size, _ = self.instructions[a]
                nxt = list(succ.get(a, []))
                if mnem.split('.')[0] not in ENDS:
                    nxt.append(a + size)
                for b in nxt:
                    if b in self.instructions and b not in rank:
                        rank[b] = r
                        work.append(b)

        dropped = set()
        addrs = sorted(self.instructions)
        for a in addrs:
            if a in dropped:
                continue
            size = self.instructions[a][2]
            for b in range(a + 2, a + size, 2):
                if b in self.instructions and b not in dropped:
                    ra, rb = rank.get(a, 99), rank.get(b, 99)
                    if ra != rb:
                        dropped.add(b if rb > ra else a)
        for a in dropped:
            del self.instructions[a]
        if dropped:
            print(f"  dropped {len(dropped)} instructions overlapping better-founded code")
        return dropped

    def _covered(self):
        """Byte addresses inside decoded instructions (excluding their starts)."""
        cov = set()
        for addr, (_, _, size, _) in self.instructions.items():
            cov.update(range(addr + 1, addr + size))
        return cov

    def _plausible(self, t):
        if not (0x200 <= t < self.rom.size) or (t & 1):
            return False
        if t in self.instructions:
            return True
        if not hasattr(self, '_cov_cache') or self._cov_n != len(self.instructions):
            self._cov_cache, self._cov_n = self._covered(), len(self.instructions)
        return t not in self._cov_cache and self._in_code_range(t)

    def _in_code_range(self, t):
        return any(lo <= t < hi for lo, hi in self.CODE_RANGES)

    def _descend(self, starts, all_func_entries):
        work = list(starts)
        while work:
            new_work = []
            for addr in work:
                if addr in self.visited or addr >= self.rom.size or addr < 0x200 or (addr & 1):
                    continue
                for target, is_call in self._disassemble_block(addr):
                    if target not in self.visited and 0x200 <= target < self.rom.size and not (target & 1):
                        new_work.append(target)
                        if is_call:
                            all_func_entries.add(target)
            work = new_work

    def _scan_jump_tables(self):
        """Scan ROM for potential jump/address tables."""
        targets = set()
        rom = self.rom

        # Look for sequences of 3+ longword addresses pointing into ROM code area
        # (code area appears to be $0E8000+ based on entry point)
        i = 0x200
        while i < rom.size - 12:
            addrs = []
            j = i
            while j < rom.size - 3:
                val = struct.unpack('>I', rom.data[j:j+4])[0]
                if 0x200 <= val < rom.size and (val & 1) == 0:
                    addrs.append(val)
                    j += 4
                else:
                    break
            if len(addrs) >= 3:
                # Heuristic: tables within code area are more likely to be real
                code_range = sum(1 for a in addrs if self._in_code_range(a))
                if code_range >= len(addrs) * 0.5 and i not in self.instructions:
                    self.jump_tables[i] = addrs
                    for a in addrs:
                        targets.add(a)
                        if a not in self.labels:
                            self.labels[a] = f"jt_{a:06X}"
                i = j
            else:
                i += 2

        print(f"Found {len(self.jump_tables)} jump tables with {len(targets)} unique targets")
        return targets

    def _scan_address_loads(self):
        """Scan disassembled code for LEA/MOVE.L #addr patterns that reference code."""
        targets = set()
        for addr, (mnemonic, op_str, size, raw) in self.instructions.items():
            if mnemonic.split('.')[0] in ('lea', 'pea'):
                # Look for absolute addresses in operand
                target = self._parse_absolute_addr(op_str)
                if target and 0x200 <= target < self.rom.size and not (target & 1):
                    targets.add(target)
            elif mnemonic == 'move.l' and '#$' in op_str:
                # move.l #$XXXXXX, ...
                try:
                    imm_str = op_str.split('#$')[1].split(',')[0].strip()
                    val = int(imm_str, 16)
                    if 0x200 <= val < self.rom.size and not (val & 1):
                        targets.add(val)
                except (ValueError, IndexError):
                    pass
        return targets

    def _scan_prologues(self):
        """Scan unvisited ROM for common function prologue patterns.

        Looks for LINK A5/A6 ($4E55/$4E56) and MOVEM.L regs,-(SP) ($48E7)
        at even addresses in the code area that haven't been visited yet.
        Borrowed from CPS1 recomp's approach.
        """
        targets = set()
        rom = self.rom
        cov = self._covered()
        i = 0x200
        while i < rom.size - 4:
            if i not in self.visited and i not in cov and (i & 1) == 0 and self._in_code_range(i):
                word = rom.read16(i)
                if word in (0x4E55, 0x4E56):
                    # LINK A5/A6 — classic function prologue
                    targets.add(i)
                    if i not in self.labels:
                        self.labels[i] = f"sub_{i:06X}"
                elif word == 0x48E7:
                    # MOVEM.L regs,-(SP) — register save prologue
                    targets.add(i)
                    if i not in self.labels:
                        self.labels[i] = f"sub_{i:06X}"
            i += 2
        return targets - self.visited

    def _parse_absolute_addr(self, op_str):
        """Address in an absolute or PC-relative source operand, else None.

        Capstone resolves PC-relative operands to absolute ones ("$ff148(pc)").
        Register-based displacements ("$e(a3)") are not addresses.
        """
        m = re.match(r'\s*\(?\$([0-9a-fA-F]+)\)?(?:\(pc\)|\.[lw])?\s*(?:,|$)', op_str)
        return int(m.group(1), 16) & 0xFFFFFF if m else None

    def _disassemble_block(self, start_addr):
        """Disassemble a basic block. Returns list of (target, is_call)."""
        targets = []
        addr = start_addr
        max_insns = 10000  # safety limit

        for _ in range(max_insns):
            if addr >= self.rom.size or addr in self.visited:
                break
            if addr & 1:
                break

            self.visited.add(addr)
            code = bytes(self.rom.data[addr:min(addr+10, self.rom.size)])
            insns = list(self.cs.disasm(code, addr, count=1))

            if not insns:
                break

            insn = insns[0]
            mnemonic = insn.mnemonic.lower()
            base = mnemonic.split('.')[0]
            op_str = insn.op_str

            self.instructions[addr] = (mnemonic, op_str, insn.size, code[:insn.size])

            if base in self.CALL_MNEMONICS:
                target = self._extract_branch_target(insn, addr)
                if target is not None:
                    self.xrefs_to[target].add(addr)
                    self.call_graph[start_addr].add(target)
                    targets.append((target, True))
                    if target not in self.labels:
                        self.labels[target] = f"sub_{target:06X}"
                addr += insn.size
                continue

            elif base == 'jmp' and '(pc,' in op_str.replace(' ', '').lower():
                # Computed jump through a PC-relative index: a switch table
                cases = self._pc_switch(addr, op_str)
                if cases:
                    self.pc_switches[addr] = cases
                    for t in cases:
                        self.xrefs_to[t].add(addr)
                        targets.append((t, False))
                        self.labels.setdefault(t, f"loc_{t:06X}")
                break

            elif base in ('bra', 'jmp'):
                target = self._extract_branch_target(insn, addr)
                if target is not None:
                    self.xrefs_to[target].add(addr)
                    targets.append((target, False))
                break

            elif mnemonic in ('rts', 'rte', 'rtr'):
                break

            elif mnemonic.startswith('b') and base not in self.BIT_MNEMONICS:
                target = self._extract_branch_target(insn, addr)
                if target is not None:
                    self.xrefs_to[target].add(addr)
                    targets.append((target, False))
                    if target not in self.labels:
                        self.labels[target] = f"loc_{target:06X}"
                addr += insn.size
                continue

            elif mnemonic.startswith('db'):
                target = self._extract_branch_target(insn, addr)
                if target is not None:
                    self.xrefs_to[target].add(addr)
                    targets.append((target, False))
                    if target not in self.labels:
                        self.labels[target] = f"loc_{target:06X}"
                addr += insn.size
                continue

            else:
                addr += insn.size

        return targets

    def _pc_switch(self, addr, op_str):
        """Case targets of a compiled switch: 'jmp T(pc,dN.w)' after
        'move.w T(pc,dM.l),dN', with a table of signed word offsets from T
        right after the JMP. The table ends where the first case begins.
        """
        m = re.match(r'\s*\$([0-9a-fA-F]+)\(pc', op_str)
        if not m:
            return None
        base = int(m.group(1), 16)
        prev = self.instructions.get(addr - 4)
        if not prev or not prev[0].startswith('move.w') or f'${base:x}(pc' not in prev[1].replace(' ', ''):
            return None
        cases, i, end = [], base, None
        while i + 2 <= self.rom.size and (end is None or i < end) and len(cases) < 256:
            t = base + struct.unpack('>h', self.rom.data[i:i + 2])[0]
            if t & 1 or not (base < t < base + 0x10000):
                break
            cases.append(t)
            end = t if end is None else min(end, t)
            i += 2
        return cases or None

    def _extract_branch_target(self, insn, addr):
        """Extract absolute target address from branch/call instruction."""
        op_str = insn.op_str.strip()

        # For DBcc: target is after the comma (e.g. "d1, $238")
        if ',' in op_str and insn.mnemonic.lower().startswith('db'):
            target_part = op_str.split(',', 1)[1].strip()
            if target_part.startswith('$'):
                try:
                    return int(target_part[1:], 16) & 0xFFFFFF
                except ValueError:
                    pass
            if target_part.startswith('0x'):
                try:
                    return int(target_part, 16) & 0xFFFFFF
                except ValueError:
                    pass

        # PC-relative with no index ("$1826(pc)"): Capstone has already
        # resolved it to the absolute target. With an index it is computed.
        m = re.fullmatch(r'\$([0-9a-fA-F]+)\(pc\)', op_str.replace(' ', '').lower())
        if m:
            return int(m.group(1), 16) & 0xFFFFFF
        if op_str.startswith('$'):
            try:
                return int(op_str[1:], 16) & 0xFFFFFF
            except ValueError:
                pass
        if op_str.startswith('0x'):
            try:
                return int(op_str, 16) & 0xFFFFFF
            except ValueError:
                pass

        # Absolute addressing modes: ($XXXX).l or ($XXXX).w
        if '(' in op_str and '$' in op_str:
            inner = op_str.replace('(', '').replace(')', '').replace('.l', '').replace('.w', '').strip()
            if inner.startswith('$'):
                try:
                    return int(inner[1:], 16) & 0xFFFFFF
                except ValueError:
                    pass

        if insn.operands:
            op = insn.operands[0]
            if hasattr(op, 'imm'):
                return op.imm & 0xFFFFFF
            if hasattr(op, 'mem') and hasattr(op.mem, 'disp'):
                if op.mem.base == 0:
                    return op.mem.disp & 0xFFFFFF

        return None

    def _build_functions(self, func_entries):
        """Build function objects from discovered entry points."""
        all_addrs = sorted(self.instructions.keys())
        if not all_addrs:
            return

        sorted_entries = sorted(func_entries & set(all_addrs))

        for i, entry in enumerate(sorted_entries):
            next_entry = sorted_entries[i + 1] if i + 1 < len(sorted_entries) else self.rom.size

            insn_addrs = []
            for addr in all_addrs:
                if addr < entry:
                    continue
                if addr >= next_entry:
                    break
                insn_addrs.append(addr)

            if not insn_addrs:
                continue

            end_addr = insn_addrs[-1]
            last_mnem = self.instructions[end_addr][0]
            last_size = self.instructions[end_addr][2]

            name = self.labels.get(entry, f"sub_{entry:06X}")

            # Collect all branch targets within this function (for local labels)
            local_labels = set()
            for ia in insn_addrs:
                for ref_from in self.xrefs_to.get(ia, set()):
                    if entry <= ref_from < next_entry:
                        local_labels.add(ia)

            self.functions[entry] = {
                'name': name,
                'start': entry,
                'end': end_addr + last_size,
                'size': (end_addr + last_size) - entry,
                'insn_count': len(insn_addrs),
                'insn_addrs': insn_addrs,
                'calls': sorted(self.call_graph.get(entry, set())),
                'has_return': last_mnem in ('rts', 'rte', 'rtr'),
                'local_labels': sorted(local_labels),
            }

    def get_disassembly(self, start=None, end=None):
        """Get formatted disassembly text."""
        lines = []
        addrs = sorted(self.instructions.keys())
        if start is not None:
            addrs = [a for a in addrs if a >= start]
        if end is not None:
            addrs = [a for a in addrs if a <= end]

        for addr in addrs:
            mnemonic, op_str, size, raw = self.instructions[addr]
            hex_bytes = ' '.join(f'{b:02X}' for b in raw)

            label = ""
            if addr in self.labels:
                label = f"\n{self.labels[addr]}:\n"

            xref = ""
            if addr in self.xrefs_to and len(self.xrefs_to[addr]) > 0:
                refs = ', '.join(f'${r:06X}' for r in sorted(self.xrefs_to[addr])[:5])
                xref = f"  ; xref: {refs}"

            lines.append(f"{label}  {addr:06X}:  {hex_bytes:<24s}  {mnemonic:<8s} {op_str}{xref}")

        return '\n'.join(lines)

    def export_json(self, path):
        """Export function map to JSON."""
        output = {
            'rom': {
                'title': self.rom.title_domestic,
                'serial': self.rom.serial,
                'size': self.rom.size,
                'entry_pc': self.rom.initial_pc,
                'initial_sp': self.rom.initial_sp,
                'checksum': self.rom.checksum,
            },
            'vectors': {name: f"0x{addr:06X}" for name, addr in self.rom.vectors.items()},
            'stats': {
                'total_instructions': len(self.instructions),
                'total_functions': len(self.functions),
                'total_xrefs': len(self.xrefs_to),
                'code_coverage_bytes': sum(self.instructions[a][2] for a in self.instructions),
            },
            'functions': {},
        }

        for addr in sorted(self.functions.keys()):
            func = self.functions[addr]
            output['functions'][f"0x{addr:06X}"] = {
                'name': func['name'],
                'start': f"0x{func['start']:06X}",
                'end': f"0x{func['end']:06X}",
                'size': func['size'],
                'insn_count': func['insn_count'],
                'calls': [f"0x{c:06X}" for c in func['calls']],
                'has_return': func['has_return'],
            }

        with open(path, 'w') as f:
            json.dump(output, f, indent=2)
        print(f"\nExported {len(self.functions)} functions to {path}")


def main():
    import argparse
    parser = argparse.ArgumentParser(description='genrecomp 68K analyzer')
    parser.add_argument('rom', help='Path to Genesis ROM file')
    parser.add_argument('--output', '-o', help='Also write the function map as JSON (ROM-derived: never commit it)')
    parser.add_argument('--config', '-c', help='Title config JSON (seeds, code_ranges)')
    parser.add_argument('--disasm', '-d', action='store_true', help='Print full disassembly')
    parser.add_argument('--disasm-func', type=str, help='Disassemble specific function (hex addr)')
    parser.add_argument('--stats', action='store_true', help='Print statistics')
    args = parser.parse_args()

    rom = GenesisROM(args.rom)
    rom.print_info()

    analyzer = M68KAnalyzer(rom)
    if args.config:
        analyzer.load_config(json.load(open(args.config)))
    analyzer.analyze()

    if args.stats:
        print("\n=== Top 20 Largest Functions ===")
        funcs_by_size = sorted(analyzer.functions.values(), key=lambda f: f['insn_count'], reverse=True)
        for f in funcs_by_size[:20]:
            print(f"  {f['name']:<30s}  ${f['start']:06X}  {f['insn_count']:5d} insns  {f['size']:5d} bytes")

        print(f"\n=== Call Graph Stats ===")
        total_calls = sum(len(f['calls']) for f in analyzer.functions.values())
        print(f"  Total call edges: {total_calls}")
        leaf = sum(1 for f in analyzer.functions.values() if len(f['calls']) == 0)
        print(f"  Leaf functions (no calls): {leaf}")

        total_bytes = sum(analyzer.instructions[a][2] for a in analyzer.instructions)
        print(f"\n=== Coverage ===")
        print(f"  Code bytes discovered: {total_bytes} ({total_bytes/1024:.1f} KB)")
        print(f"  ROM utilization:       {total_bytes*100/rom.size:.1f}%")

    if args.disasm:
        print("\n=== Full Disassembly ===")
        print(analyzer.get_disassembly())

    if args.disasm_func:
        addr = int(args.disasm_func, 16)
        if addr in analyzer.functions:
            func = analyzer.functions[addr]
            print(f"\n=== {func['name']} (${func['start']:06X} - ${func['end']:06X}) ===")
            print(analyzer.get_disassembly(func['start'], func['end']))
        else:
            print(f"No function at ${addr:06X}")

    if args.output:
        analyzer.export_json(args.output)


if __name__ == '__main__':
    main()
