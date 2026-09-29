#!/usr/bin/env python3
"""
genrecomp 68K recompiler: turns a Genesis ROM into C source for genrecomp.

One C function per discovered entry point, every instruction a statement on
g_m68k and the bus, calls through func_table_call and jumps out through
func_table_tail. Design and the traps it avoids: docs/recompiler.md.

Usage:
    python generate.py <rom> -o src/recomp [--config recomp.json]

The output is derived from the ROM. It belongs in the title's gitignored
build tree, never in a repository: each user generates it from their own ROM.
"""

import struct
import sys
import os
import re
from collections import defaultdict
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000
from capstone.m68k import *

# Import the analyzer
sys.path.insert(0, os.path.dirname(__file__))
from analyze import GenesisROM, M68KAnalyzer


# ============================================================
# M68K → C instruction translator
# ============================================================

class M68KTranslator:
    """Translates individual M68K instructions to C statements using genrecomp macros."""

    # Map capstone condition codes to M68K_CC_* macros
    CC_MAP = {
        'hi': 'M68K_CC_HI', 'ls': 'M68K_CC_LS',
        'cc': 'M68K_CC_CC', 'cs': 'M68K_CC_CS',
        'ne': 'M68K_CC_NE', 'eq': 'M68K_CC_EQ',
        'vc': 'M68K_CC_VC', 'vs': 'M68K_CC_VS',
        'pl': 'M68K_CC_PL', 'mi': 'M68K_CC_MI',
        'ge': 'M68K_CC_GE', 'lt': 'M68K_CC_LT',
        'gt': 'M68K_CC_GT', 'le': 'M68K_CC_LE',
    }

    SIZE_SUFFIX = {'.b': 8, '.w': 16, '.l': 32}

    def __init__(self, rom, labels):
        self.rom = rom
        self.labels = labels  # addr -> label name
        self.func_start = 0
        self.func_end = 0

    def translate_instruction(self, addr, mnemonic, op_str, raw_bytes, func_start, func_end=0):
        """Translate one M68K instruction to C code. Returns list of C lines."""
        self.func_start = func_start
        self.func_end = func_end
        lines = []

        # Add label if this address is a branch target
        # (unreferenced ones are dropped after the function is assembled)
        if addr in self.labels:
            lines.append(f"{self.labels[addr]}:")

        # Parse size suffix
        size = 16  # default word
        base_mnem = mnemonic
        for suffix, sz in self.SIZE_SUFFIX.items():
            if mnemonic.endswith(suffix):
                size = sz
                base_mnem = mnemonic[:-2]
                break

        # Dispatch to handler
        c_code = self._translate(addr, base_mnem, mnemonic, op_str, size, raw_bytes)
        if c_code:
            lines.append(f"    {c_code}")
        else:
            # Fallback: emit as comment
            hex_str = ' '.join(f'{b:02X}' for b in raw_bytes)
            lines.append(f"    /* TODO ${addr:06X}: {mnemonic} {op_str}  [{hex_str}] */")

        return lines

    def _translate(self, addr, base, mnemonic, op_str, size, raw_bytes):
        """Core translation dispatch."""
        self._ret = addr + len(raw_bytes)   # return address for BSR/JSR
        ops = self._split_operands(op_str) if op_str else []

        # ---- MOVE family ----
        # Check SR/CCR first before generic move
        if base == 'move' and any(o.strip().lower() in ('sr', 'ccr') for o in ops):
            return self._gen_move_sr(ops, mnemonic)
        if base == 'move':
            return self._gen_move(ops, size)
        if base == 'movea':
            return self._gen_movea(ops, size)
        if base == 'moveq':
            return self._gen_moveq(ops)
        if mnemonic == 'movem.l' or mnemonic == 'movem.w':
            return self._gen_movem(ops, mnemonic, size)
        if base == 'clr':
            return self._gen_clr(ops, size)
        if base == 'lea':
            return self._gen_lea(ops)
        if base == 'pea':
            return self._gen_pea(ops)
        if base == 'exg' or mnemonic == 'exg':
            return self._gen_exg(ops)
        if base == 'ext':
            return self._gen_ext(ops, size)
        if mnemonic == 'swap':
            return self._gen_swap(ops)
        if base == 'link':
            return self._gen_link(ops)
        if mnemonic == 'unlk':
            return self._gen_unlk(ops)

        # ---- Arithmetic ----
        if base == 'add':
            return self._gen_arith('ADD', ops, size)
        if base == 'adda':
            return self._gen_adda(ops, size)
        if base == 'addi':
            return self._gen_arith('ADD', ops, size)
        if base == 'addq':
            return self._gen_addq(ops, size)
        if base == 'addx':
            return self._gen_arith('ADDX', ops, size)
        if base == 'sub':
            return self._gen_arith('SUB', ops, size)
        if base == 'suba':
            return self._gen_suba(ops, size)
        if base == 'subi':
            return self._gen_arith('SUB', ops, size)
        if base == 'subq':
            return self._gen_subq(ops, size)
        if base == 'subx':
            return self._gen_arith('SUBX', ops, size)
        if base == 'cmp':
            return self._gen_cmp(ops, size)
        if base == 'cmpa':
            return self._gen_cmpa(ops, size)
        if base == 'cmpi':
            return self._gen_cmp(ops, size)
        if base == 'neg':
            return self._gen_neg('NEG', ops, size)
        if base == 'negx':
            return self._gen_neg('NEGX', ops, size)
        if base == 'mulu':
            return self._gen_mul('MULU', ops)
        if base == 'muls':
            return self._gen_mul('MULS', ops)
        if base == 'divu':
            return self._gen_div('DIVU', ops)
        if base == 'divs':
            return self._gen_div('DIVS', ops)

        # ---- Logic ----
        # Check for ANDI/ORI/EORI to SR/CCR first
        if base in ('andi', 'ori', 'eori') and len(ops) == 2:
            dst_lower = ops[1].strip().lower()
            if dst_lower == 'sr':
                imm = self._imm(ops[0])
                if base == 'andi':
                    return f'm68k_set_sr(m68k_get_sr() & {imm});'
                elif base == 'ori':
                    return f'm68k_set_sr(m68k_get_sr() | {imm});'
                else:
                    return f'm68k_set_sr(m68k_get_sr() ^ {imm});'
            if dst_lower == 'ccr':
                imm = self._imm(ops[0])
                if base == 'andi':
                    return f'm68k_set_ccr(m68k_get_ccr() & {imm});'
                elif base == 'ori':
                    return f'm68k_set_ccr(m68k_get_ccr() | {imm});'
                else:
                    return f'm68k_set_ccr(m68k_get_ccr() ^ {imm});'
        if base in ('and', 'andi'):
            return self._gen_logic('AND', ops, size)
        if base in ('or', 'ori'):
            return self._gen_logic('OR', ops, size)
        if base in ('eor', 'eori'):
            return self._gen_logic('EOR', ops, size)
        if base == 'not':
            return self._gen_unary_logic('NOT', ops, size)
        if base == 'tst':
            return self._gen_tst(ops, size)
        if base in ('abcd', 'sbcd'):
            return self._gen_bcd(base.upper(), ops)
        if base == 'nbcd':
            return self._gen_nbcd(ops)
        if base == 'cmpm':
            return self._gen_cmpm(ops, size)
        if base == 'movep':
            return self._gen_movep(ops, size)
        if base == 'chk':
            return self._gen_chk(ops)

        # ---- Bit operations ----
        if base == 'btst':
            return self._gen_btst(ops)
        if base == 'bset':
            return self._gen_bset(ops)
        if base == 'bclr':
            return self._gen_bclr(ops)
        if base == 'bchg':
            return self._gen_bchg(ops)

        # ---- Shifts ----
        if base == 'lsl':
            return self._gen_shift('LSL', ops, size)
        if base == 'lsr':
            return self._gen_shift('LSR', ops, size)
        if base == 'asl':
            return self._gen_shift('LSL', ops, size)  # ASL == LSL
        if base == 'asr':
            return self._gen_shift('ASR', ops, size)
        if base == 'rol':
            return self._gen_shift('ROL', ops, size)
        if base == 'ror':
            return self._gen_shift('ROR', ops, size)
        if base == 'roxl':
            return self._gen_shift('ROXL', ops, size)
        if base == 'roxr':
            return self._gen_shift('ROXR', ops, size)

        # ---- Branch ----
        if base == 'bra':
            return self._gen_bra(ops, addr)
        if base == 'bsr':
            return self._gen_bsr(ops, addr)
        if base == 'jmp':
            return self._gen_jmp(ops, addr)
        if base == 'jsr':
            return self._gen_jsr(ops, addr)
        if mnemonic == 'rts':
            return '{ func_table_rts(); return; }'
        if mnemonic == 'rte':
            return 'return; /* RTE */'
        if mnemonic == 'rtr':
            return '{ uint16_t _sr = bus_read16(g_m68k.a[7]); g_m68k.a[7] += 2; m68k_set_ccr((uint8_t)_sr); func_table_rts(); return; }'

        # Conditional branches
        for cc_suffix, cc_macro in self.CC_MAP.items():
            if base == f'b{cc_suffix}':
                return self._gen_bcc(cc_macro, ops, addr)
            if base == f's{cc_suffix}':
                return self._gen_scc(cc_macro, ops)
            if base == f'db{cc_suffix}':
                return self._gen_dbcc(cc_macro, ops, addr)

        if mnemonic == 'dbra':
            return self._gen_dbcc('0', ops, addr)  # DBRA = DBF
        if base == 'st' or mnemonic == 'st':
            return self._gen_st(ops)
        if base == 'sf' or mnemonic == 'sf':
            return self._gen_sf(ops)

        # ---- Stack ----
        if mnemonic == 'nop':
            return '/* nop */'

        # ---- SR/CCR ----
        if 'andi' in mnemonic and 'sr' in op_str.lower():
            return self._gen_andi_sr(ops)
        if 'ori' in mnemonic and 'sr' in op_str.lower():
            return self._gen_ori_sr(ops)

        # ---- TRAP ----
        if base == 'trap':
            # TRAP #N uses vector 32+N (address (32+N)*4 in vector table)
            trap_num = self._imm(ops[0])
            return f'recomp_m68k_exception(32 + {trap_num}); /* TRAP #{trap_num} */'

        return None  # unhandled

    # ================================================================
    # Operand helpers
    # ================================================================

    @staticmethod
    def _split_operands(op_str):
        """Split operand string by comma, respecting parentheses nesting."""
        parts = []
        depth = 0
        current = []
        for ch in op_str:
            if ch == '(' or ch == '[':
                depth += 1
                current.append(ch)
            elif ch == ')' or ch == ']':
                depth -= 1
                current.append(ch)
            elif ch == ',' and depth == 0:
                parts.append(''.join(current).strip())
                current = []
            else:
                current.append(ch)
        if current:
            parts.append(''.join(current).strip())
        return parts

    @staticmethod
    def _parse_reglist(reglist_str):
        """Parse M68K register list string into list of register names.
        E.g. 'd0-d3/a2-a4' -> ['d0','d1','d2','d3','a2','a3','a4']
        """
        regs = []
        # Split by '/'
        for part in reglist_str.replace(' ', '').lower().split('/'):
            m = re.match(r'^([da])(\d)-([da])(\d)$', part)
            if m:
                kind = m.group(1)
                start = int(m.group(2))
                end = int(m.group(4))
                for i in range(start, end + 1):
                    regs.append(f'{kind}{i}')
            else:
                m2 = re.match(r'^([da])(\d)$', part)
                if m2:
                    regs.append(f'{m2.group(1)}{m2.group(2)}')
                elif part == 'sp':
                    regs.append('a7')
        return regs

    def _reg(self, op):
        """Convert register name to C expression."""
        op = op.strip().lower()
        if re.match(r'^d[0-7]$', op):
            return f'g_m68k.d[{op[1]}]'
        if re.match(r'^a[0-7]$', op):
            return f'g_m68k.a[{op[1]}]'
        if op == 'sp':
            return 'g_m68k.a[7]'
        if op == 'sr':
            return 'm68k_get_sr()'
        if op == 'ccr':
            return 'm68k_get_ccr()'
        if op == 'usp':
            return 'g_m68k.usp'
        return None

    def _imm(self, op):
        """Extract immediate value."""
        op = op.strip()
        if op.startswith('#$'):
            return f'0x{op[2:]}'
        if op.startswith('#0x'):
            return op[1:]
        if op.startswith('#-$'):
            return f'(-0x{op[3:]})'
        if op.startswith('#-'):
            return op[1:]
        if op.startswith('#'):
            return op[1:]
        return None

    def _ea_read(self, op, size):
        """Generate C expression to READ from an effective address."""
        op = op.strip()

        # Register direct
        r = self._reg(op)
        if r:
            return r

        # Immediate
        imm = self._imm(op)
        if imm:
            return imm

        # (An) - register indirect
        m = re.match(r'^\(a([0-7])\)$', op, re.I)
        if m:
            return f'bus_read{size}(g_m68k.a[{m.group(1)}])'

        # (An)+ - postincrement (use helper to avoid GCC statement expressions)
        m = re.match(r'^\(a([0-7])\)\+$', op, re.I)
        if m:
            n = m.group(1)
            return f'_postinc{size}({n})'

        # -(An) - predecrement
        m = re.match(r'^-\(a([0-7])\)$', op, re.I)
        if m:
            n = m.group(1)
            return f'_predec{size}({n})'

        # d(An) - displacement
        m = re.match(r'^(-?\$?[\dA-Fa-f]+)\(a([0-7])\)$', op, re.I)
        if m:
            disp = self._parse_disp(m.group(1))
            return f'bus_read{size}(g_m68k.a[{m.group(2)}] + {disp})'

        # d(An,Xn.s) or d(An,Xn.s * scale) - indexed with data or address register
        m = re.match(r'^(-?\$?[\dA-Fa-f]*)\(a([0-7]),\s*([da])([0-7])(?:\.(w|l))?(?:\s*\*\s*(\d+))?\)$', op, re.I)
        if m:
            disp = self._parse_disp(m.group(1)) if m.group(1) else '0'
            idx_kind = m.group(3).lower()
            idx_num = m.group(4)
            idx_size = (m.group(5) or 'w').lower()
            scale = m.group(6)
            if idx_kind == 'd':
                idx = f'g_m68k.d[{idx_num}]'
            else:
                idx = f'g_m68k.a[{idx_num}]'
            if idx_size == 'w':
                idx = f'(int16_t)(uint16_t){idx}'
            else:
                idx = f'(int32_t){idx}'
            if scale and scale != '1':
                idx = f'{idx} * {scale}'
            return f'bus_read{size}(g_m68k.a[{m.group(2)}] + {disp} + {idx})'

        # d(PC) - PC relative
        m = re.match(r'^(-?\$?[\dA-Fa-f]+)\(pc\)$', op, re.I)
        if m:
            disp = self._parse_disp(m.group(1))
            return f'bus_read{size}({disp})'  # PC-relative resolved by capstone

        # d(PC, Xn.s) - PC relative indexed
        m = re.match(r'^(-?\$?[\dA-Fa-f]*)\(pc,\s*([da])([0-7])(?:\.(w|l))?(?:\s*\*\s*(\d+))?\)$', op, re.I)
        if m:
            disp = self._parse_disp(m.group(1)) if m.group(1) else '0'
            idx_kind = m.group(2).lower()
            idx_num = m.group(3)
            idx_size = (m.group(4) or 'w').lower()
            scale = m.group(5)
            if idx_kind == 'd':
                idx = f'g_m68k.d[{idx_num}]'
            else:
                idx = f'g_m68k.a[{idx_num}]'
            if idx_size == 'w':
                idx = f'(int16_t)(uint16_t){idx}'
            else:
                idx = f'(int32_t){idx}'
            if scale and scale != '1':
                idx = f'{idx} * {scale}'
            return f'bus_read{size}({disp} + {idx})'

        # ($XXXX).w or ($XXXX).l - absolute
        m = re.match(r'^\(\$([0-9A-Fa-f]+)\)\.(w|l)$', op, re.I)
        if m:
            addr_val = int(m.group(1), 16)
            return f'bus_read{size}(0x{addr_val:06X})'

        # $XXXX.l or $XXXX - absolute (no parens)
        m = re.match(r'^\$([0-9A-Fa-f]+)(?:\.(w|l))?$', op, re.I)
        if m:
            addr_val = int(m.group(1), 16)
            return f'bus_read{size}(0x{addr_val:06X})'

        return f'/* UNHANDLED_READ: {op} */ 0'

    def _ea_write(self, op, size, value_expr):
        """Generate C statement to WRITE to an effective address."""
        op = op.strip()

        # Register direct
        r = self._reg(op)
        if r:
            if size == 8:
                return f'{r} = ({r} & 0xFFFFFF00u) | ((uint8_t)({value_expr}));'
            elif size == 16:
                return f'{r} = ({r} & 0xFFFF0000u) | ((uint16_t)({value_expr}));'
            else:
                return f'{r} = {value_expr};'

        # (An)
        m = re.match(r'^\(a([0-7])\)$', op, re.I)
        if m:
            return f'bus_write{size}(g_m68k.a[{m.group(1)}], {value_expr});'

        # (An)+
        m = re.match(r'^\(a([0-7])\)\+$', op, re.I)
        if m:
            n = m.group(1)
            inc = 2 if (size == 8 and n == '7') else size // 8   # A7 stays word-aligned
            return f'bus_write{size}(g_m68k.a[{n}], {value_expr}); g_m68k.a[{n}] += {inc};'

        # -(An)
        m = re.match(r'^-\(a([0-7])\)$', op, re.I)
        if m:
            n = m.group(1)
            dec = 2 if (size == 8 and n == '7') else size // 8   # A7 stays word-aligned
            return f'g_m68k.a[{n}] -= {dec}; bus_write{size}(g_m68k.a[{n}], {value_expr});'

        # d(An)
        m = re.match(r'^(-?\$?[\dA-Fa-f]+)\(a([0-7])\)$', op, re.I)
        if m:
            disp = self._parse_disp(m.group(1))
            return f'bus_write{size}(g_m68k.a[{m.group(2)}] + {disp}, {value_expr});'

        # d(An,Xn.s) or d(An,Xn.s * scale) - indexed with data or address register
        m = re.match(r'^(-?\$?[\dA-Fa-f]*)\(a([0-7]),\s*([da])([0-7])(?:\.(w|l))?(?:\s*\*\s*(\d+))?\)$', op, re.I)
        if m:
            disp = self._parse_disp(m.group(1)) if m.group(1) else '0'
            idx_kind = m.group(3).lower()
            idx_num = m.group(4)
            idx_size = (m.group(5) or 'w').lower()
            scale = m.group(6)
            if idx_kind == 'd':
                idx = f'g_m68k.d[{idx_num}]'
            else:
                idx = f'g_m68k.a[{idx_num}]'
            if idx_size == 'w':
                idx = f'(int16_t)(uint16_t){idx}'
            else:
                idx = f'(int32_t){idx}'
            if scale and scale != '1':
                idx = f'{idx} * {scale}'
            return f'bus_write{size}(g_m68k.a[{m.group(2)}] + {disp} + {idx}, {value_expr});'

        # Absolute
        m = re.match(r'^\(\$([0-9A-Fa-f]+)\)\.(w|l)$', op, re.I)
        if m:
            addr_val = int(m.group(1), 16)
            return f'bus_write{size}(0x{addr_val:06X}, {value_expr});'

        m = re.match(r'^\$([0-9A-Fa-f]+)(?:\.(w|l))?$', op, re.I)
        if m:
            addr_val = int(m.group(1), 16)
            return f'bus_write{size}(0x{addr_val:06X}, {value_expr});'

        return f'/* UNHANDLED_WRITE: {op} = {value_expr} */'

    def _parse_disp(self, s):
        """Parse displacement value."""
        s = s.strip()
        if not s or s == '0':
            return '0'
        neg = s.startswith('-')
        if neg:
            s = s[1:]
        if s.startswith('$'):
            val = f'0x{s[1:]}'
        elif s.startswith('0x'):
            val = s
        else:
            val = s
        return f'(-{val})' if neg else val

    def _ea_addr(self, op):
        """Generate C expression for the effective address itself (for LEA/PEA/memory RMW)."""
        op = op.strip()

        # (An)+  — for _ea_addr in RMW context, just use current An
        m = re.match(r'^\(a([0-7])\)\+$', op, re.I)
        if m:
            return f'g_m68k.a[{m.group(1)}]'

        # -(An) — for _ea_addr in RMW context, pre-decrement is caller's job
        m = re.match(r'^-\(a([0-7])\)$', op, re.I)
        if m:
            return f'g_m68k.a[{m.group(1)}]'

        m = re.match(r'^\(a([0-7])\)$', op, re.I)
        if m:
            return f'g_m68k.a[{m.group(1)}]'

        m = re.match(r'^(-?\$?[\dA-Fa-f]+)\(a([0-7])\)$', op, re.I)
        if m:
            disp = self._parse_disp(m.group(1))
            return f'(g_m68k.a[{m.group(2)}] + {disp})'

        m = re.match(r'^(-?\$?[\dA-Fa-f]*)\(a([0-7]),\s*([da])([0-7])(?:\.(w|l))?(?:\s*\*\s*(\d+))?\)$', op, re.I)
        if m:
            disp = self._parse_disp(m.group(1)) if m.group(1) else '0'
            idx_kind = m.group(3).lower()
            idx_num = m.group(4)
            idx_size = (m.group(5) or 'w').lower()
            scale = m.group(6)
            if idx_kind == 'd':
                idx = f'g_m68k.d[{idx_num}]'
            else:
                idx = f'g_m68k.a[{idx_num}]'
            if idx_size == 'w':
                idx = f'(int16_t)(uint16_t){idx}'
            else:
                idx = f'(int32_t){idx}'
            if scale and scale != '1':
                idx = f'{idx} * {scale}'
            return f'(g_m68k.a[{m.group(2)}] + {disp} + {idx})'

        m = re.match(r'^(-?\$?[\dA-Fa-f]+)\(pc\)$', op, re.I)
        if m:
            return self._parse_disp(m.group(1))

        # d(PC, Xn.s) - PC relative indexed
        m = re.match(r'^(-?\$?[\dA-Fa-f]*)\(pc,\s*([da])([0-7])(?:\.(w|l))?(?:\s*\*\s*(\d+))?\)$', op, re.I)
        if m:
            disp = self._parse_disp(m.group(1)) if m.group(1) else '0'
            idx_kind = m.group(2).lower()
            idx_num = m.group(3)
            idx_size = (m.group(4) or 'w').lower()
            scale = m.group(5)
            if idx_kind == 'd':
                idx = f'g_m68k.d[{idx_num}]'
            else:
                idx = f'g_m68k.a[{idx_num}]'
            if idx_size == 'w':
                idx = f'(int16_t)(uint16_t){idx}'
            else:
                idx = f'(int32_t){idx}'
            if scale and scale != '1':
                idx = f'{idx} * {scale}'
            return f'({disp} + {idx})'

        m = re.match(r'^\(\$([0-9A-Fa-f]+)\)\.(w|l)$', op, re.I)
        if m:
            return f'0x{int(m.group(1), 16):06X}'

        m = re.match(r'^\$([0-9A-Fa-f]+)(?:\.(w|l))?$', op, re.I)
        if m:
            return f'0x{int(m.group(1), 16):06X}'

        return f'/* UNHANDLED_ADDR: {op} */ 0'

    # ================================================================
    # Instruction generators
    # ================================================================

    def _gen_move(self, ops, size):
        if len(ops) != 2:
            return None
        src = self._ea_read(ops[0], size)
        # MOVE updates N, Z, clears V, C
        r = self._reg(ops[1])
        if r:
            stmt = self._ea_write(ops[1], size, src)
            mask = {8: '0xFF', 16: '0xFFFF', 32: '0xFFFFFFFFu'}[size]
            return f'{stmt} M68K_TST{size}({r} & {mask});'
        # Memory destination: use temp for flag testing
        return f'{{ uint{size}_t _mv = (uint{size}_t)({src}); {self._ea_write(ops[1], size, "_mv")} M68K_TST{size}(_mv); }}'

    def _gen_movea(self, ops, size):
        if len(ops) != 2:
            return None
        src = self._ea_read(ops[0], size)
        r = self._reg(ops[1])
        if r:
            if size == 16:
                return f'{r} = (uint32_t)(int32_t)(int16_t)(uint16_t)({src});'
            return f'{r} = {src};'
        return None

    def _gen_moveq(self, ops):
        if len(ops) != 2:
            return None
        imm = self._imm(ops[0])
        r = self._reg(ops[1])
        if imm and r:
            return f'{r} = (uint32_t)(int32_t)(int8_t)({imm}); M68K_TST32({r});'
        return None

    def _gen_movem(self, ops, mnemonic, size):
        byte_size = size // 8  # 2 for .w, 4 for .l
        read_fn = f'bus_read{size}'
        write_fn = f'bus_write{size}'

        # Determine direction: register-to-memory or memory-to-register
        # movem <reglist>, <ea>  -- store regs to memory
        # movem <ea>, <reglist>  -- load regs from memory

        # Check if first operand is a register list (contains d or a with ranges/slashes)
        def _is_reglist(s):
            s = s.strip().lower()
            return bool(re.match(r'^[da]\d', s)) and ('/' in s or '-' in s or re.match(r'^[da]\d$', s))

        if _is_reglist(ops[0]):
            # Store: movem <reglist>, <ea>
            reglist = self._parse_reglist(ops[0])
            ea = ops[1].strip()

            # -(An) predecrement: push in reverse order
            m = re.match(r'^-\(a([0-7])\)$', ea, re.I)
            if m:
                n = m.group(1)
                stmts = [f'{{ uint32_t _an = g_m68k.a[{n}];']
                # Reverse order: A7..A0 then D7..D0, but only regs in list
                all_regs_reverse = [f'a{i}' for i in range(7, -1, -1)] + [f'd{i}' for i in range(7, -1, -1)]
                for r in all_regs_reverse:
                    if r in reglist:
                        # 68000: An in its own list is stored as its initial value
                        reg_c = '_an' if r == f'a{n}' else self._reg(r)
                        stmts.append(f'g_m68k.a[{n}] -= {byte_size}; {write_fn}(g_m68k.a[{n}], {reg_c});')
                stmts.append('}')
                return ' '.join(stmts)

            # (An) no update: store with temp addr
            m = re.match(r'^\(a([0-7])\)$', ea, re.I)
            if m:
                n = m.group(1)
                stmts = [f'{{ uint32_t _addr = g_m68k.a[{n}];']
                for r in reglist:
                    reg_c = self._reg(r)
                    stmts.append(f'{write_fn}(_addr, {reg_c}); _addr += {byte_size};')
                stmts.append('}')
                return ' '.join(stmts)

            # d(An): store with displacement
            m = re.match(r'^(-?\$?[\dA-Fa-f]+)\(a([0-7])\)$', ea, re.I)
            if m:
                disp = self._parse_disp(m.group(1))
                n = m.group(2)
                stmts = [f'{{ uint32_t _addr = g_m68k.a[{n}] + {disp};']
                for r in reglist:
                    reg_c = self._reg(r)
                    stmts.append(f'{write_fn}(_addr, {reg_c}); _addr += {byte_size};')
                stmts.append('}')
                return ' '.join(stmts)

            return f'/* TODO: movem store to {ea} */'

        else:
            # Load: movem <ea>, <reglist>
            reglist = self._parse_reglist(ops[1])
            ea = ops[0].strip()

            # (An)+ postincrement
            m = re.match(r'^\(a([0-7])\)\+$', ea, re.I)
            if m:
                n = m.group(1)
                stmts = []
                for r in reglist:
                    reg_c = self._reg(r)
                    if r == f'a{n}':
                        stmts.append(f'g_m68k.a[{n}] += {byte_size}; /* {r} itself: memory value discarded */')
                    elif size == 16:
                        stmts.append(f'{reg_c} = (uint32_t)(int32_t)(int16_t){read_fn}(g_m68k.a[{n}]); g_m68k.a[{n}] += {byte_size};')
                    else:
                        stmts.append(f'{reg_c} = {read_fn}(g_m68k.a[{n}]); g_m68k.a[{n}] += {byte_size};')
                return ' '.join(stmts)

            # (An) no update
            m = re.match(r'^\(a([0-7])\)$', ea, re.I)
            if m:
                n = m.group(1)
                stmts = [f'{{ uint32_t _addr = g_m68k.a[{n}];']
                for r in reglist:
                    reg_c = self._reg(r)
                    if size == 16:
                        stmts.append(f'{reg_c} = (uint32_t)(int32_t)(int16_t){read_fn}(_addr); _addr += {byte_size};')
                    else:
                        stmts.append(f'{reg_c} = {read_fn}(_addr); _addr += {byte_size};')
                stmts.append('}')
                return ' '.join(stmts)

            # d(An) displacement
            m = re.match(r'^(-?\$?[\dA-Fa-f]+)\(a([0-7])\)$', ea, re.I)
            if m:
                disp = self._parse_disp(m.group(1))
                n = m.group(2)
                stmts = [f'{{ uint32_t _addr = g_m68k.a[{n}] + {disp};']
                for r in reglist:
                    reg_c = self._reg(r)
                    if size == 16:
                        stmts.append(f'{reg_c} = (uint32_t)(int32_t)(int16_t){read_fn}(_addr); _addr += {byte_size};')
                    else:
                        stmts.append(f'{reg_c} = {read_fn}(_addr); _addr += {byte_size};')
                stmts.append('}')
                return ' '.join(stmts)

            return f'/* TODO: movem load from {ea} */'

    def _gen_clr(self, ops, size):
        if len(ops) != 1:
            return None
        stmt = self._ea_write(ops[0], size, '0')
        return f'{stmt} g_m68k.flag_N = false; g_m68k.flag_Z = true; g_m68k.flag_V = false; g_m68k.flag_C = false;'

    def _gen_lea(self, ops):
        if len(ops) != 2:
            return None
        addr_expr = self._ea_addr(ops[0])
        r = self._reg(ops[1])
        if r:
            return f'{r} = {addr_expr};'
        return None

    def _gen_pea(self, ops):
        addr_expr = self._ea_addr(ops[0])
        return f'g_m68k.a[7] -= 4; bus_write32(g_m68k.a[7], {addr_expr});'

    def _gen_exg(self, ops):
        r1 = self._reg(ops[0])
        r2 = self._reg(ops[1])
        if r1 and r2:
            return f'{{ uint32_t _t = {r1}; {r1} = {r2}; {r2} = _t; }}'
        return None

    def _gen_ext(self, ops, size):
        r = self._reg(ops[0])
        if r:
            if size == 16:
                return f'M68K_EXT16({r});'
            return f'M68K_EXT32({r});'
        return None

    def _gen_swap(self, ops):
        r = self._reg(ops[0])
        if r:
            return f'M68K_SWAP({r});'
        return None

    # -(An) / (An)+ operand -> register number, else None
    @staticmethod
    def _predec(op):
        m = re.fullmatch(r'-\(a([0-7])\)', op.strip().lower())
        return int(m.group(1)) if m else None

    @staticmethod
    def _postinc(op):
        m = re.fullmatch(r'\(a([0-7])\)\+', op.strip().lower())
        return int(m.group(1)) if m else None

    def _gen_bcd(self, op, ops):
        """ABCD/SBCD Dy,Dx or -(Ay),-(Ax)."""
        if len(ops) != 2:
            return None
        src, dst = self._reg(ops[0]), self._reg(ops[1])
        if src and dst:
            return f'M68K_{op}({dst}, {src});'
        y, x = self._predec(ops[0]), self._predec(ops[1])
        if y is None or x is None:
            return None
        return (f'{{ g_m68k.a[{y}] -= 1; uint8_t _s = bus_read8(g_m68k.a[{y}]); '
                f'g_m68k.a[{x}] -= 1; uint8_t _t = bus_read8(g_m68k.a[{x}]); '
                f'M68K_{op}(_t, _s); bus_write8(g_m68k.a[{x}], _t); }}')

    def _gen_nbcd(self, ops):
        r = self._reg(ops[0])
        if r:
            return f'M68K_NBCD({r});'
        ea_addr = self._ea_addr(ops[0])
        return f'{{ uint32_t _ea = {ea_addr}; uint8_t _t = bus_read8(_ea); M68K_NBCD(_t); bus_write8(_ea, _t); }}'

    def _gen_cmpm(self, ops, size):
        """CMPM (Ay)+,(Ax)+: compare (Ax) - (Ay), both post-incremented."""
        y, x = self._postinc(ops[0]), self._postinc(ops[1])
        if y is None or x is None:
            return None
        n = size // 8
        return (f'{{ uint{size}_t _s = bus_read{size}(g_m68k.a[{y}]); g_m68k.a[{y}] += {n}; '
                f'uint{size}_t _d = bus_read{size}(g_m68k.a[{x}]); g_m68k.a[{x}] += {n}; '
                f'M68K_CMP{size}(_d, _s); }}')

    def _gen_movep(self, ops, size):
        """MOVEP: register <-> alternate bytes at d16(An), high byte first.
        Used for 8-bit peripherals on one half of the bus (the Z80 side)."""
        n = size // 8
        src_r, dst_r = self._reg(ops[0]), self._reg(ops[1])
        if src_r and not dst_r:          # Dn -> d16(An)
            ea = self._ea_addr(ops[1])
            return '{ uint32_t _ea = ' + ea + '; ' + ' '.join(
                f'bus_write8(_ea + {2 * i}, (uint8_t)({src_r} >> {8 * (n - 1 - i)}));' for i in range(n)) + ' }'
        if dst_r and not src_r:          # d16(An) -> Dn
            ea = self._ea_addr(ops[0])
            val = ' | '.join(f'((uint32_t)bus_read8(_ea + {2 * i}) << {8 * (n - 1 - i)})' for i in range(n))
            if size == 16:
                return f'{{ uint32_t _ea = {ea}; {dst_r} = ({dst_r} & 0xFFFF0000u) | (uint16_t)({val}); }}'
            return f'{{ uint32_t _ea = {ea}; {dst_r} = {val}; }}'
        return None

    def _gen_chk(self, ops):
        """CHK <ea>,Dn: trap (vector 6) if Dn < 0 or Dn > bound."""
        d = self._reg(ops[1])
        if not d:
            return None
        bound = self._ea_read(ops[0], 16)
        return (f'{{ int16_t _b = (int16_t)({bound}); int16_t _v = (int16_t){d}; '
                f'if (_v < 0 || _v > _b) {{ g_m68k.flag_N = _v < 0; recomp_m68k_exception(6); }} }}')

    def _gen_link(self, ops):
        r = self._reg(ops[0])
        imm = self._imm(ops[1])
        if r and imm:
            return f'g_m68k.a[7] -= 4; bus_write32(g_m68k.a[7], {r}); {r} = g_m68k.a[7]; g_m68k.a[7] += (int16_t)({imm});'
        return None

    def _gen_unlk(self, ops):
        r = self._reg(ops[0])
        if r:
            return f'g_m68k.a[7] = {r}; {r} = bus_read32(g_m68k.a[7]); g_m68k.a[7] += 4;'
        return None

    def _gen_arith(self, op, ops, size):
        if len(ops) != 2:
            return None
        src = self._ea_read(ops[0], size)
        dst_r = self._reg(ops[1])
        if dst_r:
            return f'M68K_{op}{size}({dst_r}, {src});'
        # Memory destination: read-modify-write
        ea_addr = self._ea_addr(ops[1])
        return (f'{{ uint32_t _ea = {ea_addr}; uint{size}_t _tmp = bus_read{size}(_ea); '
                f'M68K_{op}{size}(_tmp, {src}); bus_write{size}(_ea, _tmp); }}')

    def _gen_adda(self, ops, size):
        src = self._ea_read(ops[0], size)
        r = self._reg(ops[1])
        if r:
            if size == 16:
                return f'{r} += (int16_t)(uint16_t)({src});'
            return f'{r} += {src};'
        return None

    def _gen_suba(self, ops, size):
        src = self._ea_read(ops[0], size)
        r = self._reg(ops[1])
        if r:
            if size == 16:
                return f'{r} -= (int16_t)(uint16_t)({src});'
            return f'{r} -= {src};'
        return None

    def _gen_addq(self, ops, size):
        imm = self._imm(ops[0])
        if not imm:
            return None
        dst_r = self._reg(ops[1])
        if dst_r:
            if ops[1].strip().lower().startswith('a'):
                return f'{dst_r} += {imm};'
            return f'M68K_ADD{size}({dst_r}, {imm});'
        # Memory destination
        ea_addr = self._ea_addr(ops[1])
        return (f'{{ uint32_t _ea = {ea_addr}; uint{size}_t _tmp = bus_read{size}(_ea); '
                f'M68K_ADD{size}(_tmp, {imm}); bus_write{size}(_ea, _tmp); }}')

    def _gen_subq(self, ops, size):
        imm = self._imm(ops[0])
        if not imm:
            return None
        dst_r = self._reg(ops[1])
        if dst_r:
            if ops[1].strip().lower().startswith('a'):
                return f'{dst_r} -= {imm};'
            return f'M68K_SUB{size}({dst_r}, {imm});'
        # Memory destination
        ea_addr = self._ea_addr(ops[1])
        return (f'{{ uint32_t _ea = {ea_addr}; uint{size}_t _tmp = bus_read{size}(_ea); '
                f'M68K_SUB{size}(_tmp, {imm}); bus_write{size}(_ea, _tmp); }}')

    def _gen_cmp(self, ops, size):
        src = self._ea_read(ops[0], size)
        dst = self._ea_read(ops[1], size)
        return f'M68K_CMP{size}({dst}, {src});'

    def _gen_cmpa(self, ops, size):
        src = self._ea_read(ops[0], size)
        r = self._reg(ops[1])
        if r:
            if size == 16:
                return f'M68K_CMP32({r}, (uint32_t)(int32_t)(int16_t)(uint16_t)({src}));'
            return f'M68K_CMP32({r}, {src});'
        return None

    def _gen_neg(self, op, ops, size):
        r = self._reg(ops[0])
        if r:
            return f'M68K_{op}{size}({r});'
        # Memory destination
        ea_addr = self._ea_addr(ops[0])
        return (f'{{ uint32_t _ea = {ea_addr}; uint{size}_t _tmp = bus_read{size}(_ea); '
                f'M68K_{op}{size}(_tmp); bus_write{size}(_ea, _tmp); }}')

    def _gen_mul(self, op, ops):
        src = self._ea_read(ops[0], 16)
        r = self._reg(ops[1])
        if r:
            return f'M68K_{op}({r}, {src});'
        return None

    def _gen_div(self, op, ops):
        src = self._ea_read(ops[0], 16)
        r = self._reg(ops[1])
        if r:
            return f'M68K_{op}({r}, {src});'
        return None

    def _gen_logic(self, op, ops, size):
        src = self._ea_read(ops[0], size)
        dst_r = self._reg(ops[1])
        if dst_r:
            return f'M68K_{op}{size}({dst_r}, {src});'
        # Memory destination
        ea_addr = self._ea_addr(ops[1])
        return (f'{{ uint32_t _ea = {ea_addr}; uint{size}_t _tmp = bus_read{size}(_ea); '
                f'M68K_{op}{size}(_tmp, {src}); bus_write{size}(_ea, _tmp); }}')

    def _gen_unary_logic(self, op, ops, size):
        r = self._reg(ops[0])
        if r:
            return f'M68K_{op}{size}({r});'
        # Memory destination
        ea_addr = self._ea_addr(ops[0])
        return (f'{{ uint32_t _ea = {ea_addr}; uint{size}_t _tmp = bus_read{size}(_ea); '
                f'M68K_{op}{size}(_tmp); bus_write{size}(_ea, _tmp); }}')

    def _gen_tst(self, ops, size):
        val = self._ea_read(ops[0], size)
        return f'M68K_TST{size}({val});'

    def _gen_btst(self, ops):
        bit = self._ea_read(ops[0], 32)
        r = self._reg(ops[1])
        if r:
            # Register: BTST operates on full 32 bits
            return f'M68K_BTST32({r}, {bit});'
        # Memory: BTST always operates on a byte regardless of Capstone suffix
        val = self._ea_read(ops[1], 8)
        return f'M68K_BTST8({val}, {bit});'

    def _gen_bset(self, ops):
        bit = self._ea_read(ops[0], 32)
        r = self._reg(ops[1])
        if r:
            return f'M68K_BSET32({r}, {bit});'
        # Memory: byte-size BSET
        ea_addr = self._ea_addr(ops[1])
        return (f'{{ uint32_t _ea = {ea_addr}; uint8_t _b = 1u << (({bit}) & 7); uint8_t _v = bus_read8(_ea); '
                f'g_m68k.flag_Z = !(_v & _b); bus_write8(_ea, _v | _b); }}')

    def _gen_bclr(self, ops):
        bit = self._ea_read(ops[0], 32)
        r = self._reg(ops[1])
        if r:
            return f'M68K_BCLR32({r}, {bit});'
        # Memory: byte-size BCLR
        ea_addr = self._ea_addr(ops[1])
        return (f'{{ uint32_t _ea = {ea_addr}; uint8_t _b = 1u << (({bit}) & 7); uint8_t _v = bus_read8(_ea); '
                f'g_m68k.flag_Z = !(_v & _b); bus_write8(_ea, _v & ~_b); }}')

    def _gen_bchg(self, ops):
        bit = self._ea_read(ops[0], 32)
        r = self._reg(ops[1])
        if r:
            return f'M68K_BCHG32({r}, {bit});'
        # Memory: byte-size BCHG
        ea_addr = self._ea_addr(ops[1])
        return (f'{{ uint32_t _ea = {ea_addr}; uint8_t _b = 1u << (({bit}) & 7); uint8_t _v = bus_read8(_ea); '
                f'g_m68k.flag_Z = !(_v & _b); bus_write8(_ea, _v ^ _b); }}')

    def _gen_shift(self, op, ops, size):
        if len(ops) == 2:
            cnt = self._ea_read(ops[0], 8)
            r = self._reg(ops[1])
            if r:
                return f'M68K_{op}{size}({r}, {cnt});'
            # Memory destination
            ea_addr = self._ea_addr(ops[1])
            return (f'{{ uint32_t _ea = {ea_addr}; uint{size}_t _tmp = bus_read{size}(_ea); '
                    f'M68K_{op}{size}(_tmp, {cnt}); bus_write{size}(_ea, _tmp); }}')
        elif len(ops) == 1:
            # Memory shift by 1 (e.g. LSL.W (An))
            ea_addr = self._ea_addr(ops[0])
            return (f'{{ uint32_t _ea = {ea_addr}; uint16_t _tmp = bus_read16(_ea); '
                    f'M68K_{op}16(_tmp, 1); bus_write16(_ea, _tmp); }}')
        return None

    def _is_local_target(self, target):
        """Check if target address is within current function."""
        return self.func_start <= target < self.func_end

    def _gen_bra(self, ops, addr):
        target = self._parse_branch_target(ops[0])
        if target is not None and self._is_local_target(target):
            label = self.labels.get(target, f'loc_{target:06X}')
            return f'goto {label};'
        if target is not None:
            return f'{{ func_table_call(0x{target:06X}); return; }}'
        return f'/* BRA unresolved */'

    def _gen_bsr(self, ops, addr):
        target = self._parse_branch_target(ops[0])
        label = self.labels.get(target, f'sub_{target:06X}')
        return f'func_table_jsr(0x{target:06X}, 0x{self._ret:06X}); /* {label} */'

    def _gen_jmp(self, ops, addr):
        target = self._parse_branch_target(ops[0])
        if target is not None:
            label = self.labels.get(target, f'sub_{target:06X}')
            if label.startswith('loc_'):
                return f'goto {label};'
            return f'{{ func_table_call(0x{target:06X}); return; }} /* {label} */'
        cases = getattr(self, 'pc_switches', {}).get(addr)
        if cases:
            # Compiled switch: every case is a label in this function
            ea_addr = self._ea_addr(ops[0])
            # Cases in this region are gotos; the rest become registered
            # entry points reached by tail jump (the region can end at the
            # JMP, leaving the case code in the next one).
            body = ' '.join(
                f'case 0x{t:06X}: goto {self.labels[t]};' if self._is_local_target(t)
                else f'case 0x{t:06X}: {{ func_table_tail(0x{t:06X}); return; }}'
                for t in sorted(set(cases)) if t < self.rom.size)
            return (f'switch ({ea_addr}) {{ {body} '
                    f'default: {{ func_table_call({ea_addr}); return; }} }} /* switch via {ops[0]} */')
        # True indirect JMP through register/EA
        ea_addr = self._ea_addr(ops[0])
        if 'UNHANDLED' not in ea_addr:
            return f'{{ func_table_call({ea_addr}); return; }} /* JMP indirect via {ops[0]} */'
        return f'/* JMP indirect: {ops[0]} — TODO */'

    def _gen_jsr(self, ops, addr):
        target = self._parse_branch_target(ops[0])
        if target is not None:
            label = self.labels.get(target, f'sub_{target:06X}')
            return f'func_table_jsr(0x{target:06X}, 0x{self._ret:06X}); /* {label} */'
        # True indirect JSR through register/EA: evaluate the EA before the push
        ea_addr = self._ea_addr(ops[0])
        if 'UNHANDLED' not in ea_addr:
            return f'{{ uint32_t _t = {ea_addr}; func_table_jsr(_t, 0x{self._ret:06X}); }} /* JSR indirect via {ops[0]} */'
        return f'/* JSR indirect: {ops[0]} — TODO */'

    def _gen_bcc(self, cc_macro, ops, addr):
        target = self._parse_branch_target(ops[0])
        if target is not None and self._is_local_target(target):
            label = self.labels.get(target, f'loc_{target:06X}')
            return f'if ({cc_macro}) goto {label};'
        if target is not None:
            return f'if ({cc_macro}) {{ func_table_call(0x{target:06X}); return; }}'
        return f'/* Bcc unresolved */'

    def _gen_scc(self, cc_macro, ops):
        r = self._reg(ops[0])
        if r:
            return f'{r} = ({r} & 0xFFFFFF00u) | ({cc_macro} ? 0xFFu : 0x00u);'
        # Memory destination
        ea_addr = self._ea_addr(ops[0])
        return f'bus_write8({ea_addr}, {cc_macro} ? 0xFF : 0x00);'

    def _gen_dbcc(self, cc_macro, ops, addr):
        r = self._reg(ops[0])
        target = self._parse_branch_target(ops[1]) if len(ops) > 1 else None
        if r and target is not None:
            if self._is_local_target(target):
                label = self.labels.get(target, f'loc_{target:06X}')
                if cc_macro == '0':
                    return f'{{ int16_t _cnt = (int16_t)(uint16_t){r}; _cnt--; {r} = ({r} & 0xFFFF0000u) | (uint16_t)_cnt; if (_cnt != -1) goto {label}; }}'
                return f'if (!({cc_macro})) {{ int16_t _cnt = (int16_t)(uint16_t){r}; _cnt--; {r} = ({r} & 0xFFFF0000u) | (uint16_t)_cnt; if (_cnt != -1) goto {label}; }}'
            else:
                # Cross-function DBcc — emit as call
                if cc_macro == '0':
                    return f'{{ int16_t _cnt = (int16_t)(uint16_t){r}; _cnt--; {r} = ({r} & 0xFFFF0000u) | (uint16_t)_cnt; if (_cnt != -1) {{ func_table_call(0x{target:06X}); return; }} }}'
                return f'if (!({cc_macro})) {{ int16_t _cnt = (int16_t)(uint16_t){r}; _cnt--; {r} = ({r} & 0xFFFF0000u) | (uint16_t)_cnt; if (_cnt != -1) {{ func_table_call(0x{target:06X}); return; }} }}'
        return None

    def _gen_st(self, ops):
        r = self._reg(ops[0])
        if r:
            return f'{r} = ({r} & 0xFFFFFF00u) | 0xFFu;'
        ea_addr = self._ea_addr(ops[0])
        return f'bus_write8({ea_addr}, 0xFF);'

    def _gen_sf(self, ops):
        r = self._reg(ops[0])
        if r:
            return f'{r} = ({r} & 0xFFFFFF00u);'
        ea_addr = self._ea_addr(ops[0])
        return f'bus_write8({ea_addr}, 0x00);'

    def _gen_move_sr(self, ops, mnemonic):
        if 'sr' in ops[0].lower():
            # MOVE SR, <ea>
            return self._ea_write(ops[1], 16, 'm68k_get_sr()')
        else:
            # MOVE <ea>, SR
            src = self._ea_read(ops[0], 16)
            return f'm68k_set_sr({src});'

    def _gen_andi_sr(self, ops):
        imm = self._imm(ops[0])
        return f'm68k_set_sr(m68k_get_sr() & {imm});'

    def _gen_ori_sr(self, ops):
        imm = self._imm(ops[0])
        return f'm68k_set_sr(m68k_get_sr() | {imm});'

    def _parse_branch_target(self, op):
        op = op.strip()
        # $XXXX or $XXXX.l or $XXXX.w
        m = re.match(r'^\$([0-9A-Fa-f]+)(?:\.(w|l))?$', op)
        if m:
            try:
                return int(m.group(1), 16)
            except ValueError:
                return None
        if op.startswith('0x'):
            try:
                return int(op.split('.')[0], 16)
            except ValueError:
                return None
        # Absolute with parens: ($XXXX).l
        m = re.match(r'^\(\$([0-9A-Fa-f]+)\)\.(w|l)$', op)
        if m:
            return int(m.group(1), 16)
        return None


# ============================================================
# Code generator — produces .c and .h files
# ============================================================

class CodeGenerator:
    MAX_FUNCS_PER_FILE = 50

    def __init__(self, rom, analyzer, translator, output_dir):
        self.rom = rom
        self.analyzer = analyzer
        self.translator = translator
        self.output_dir = output_dir
        self.cross_func_targets = set()  # addresses used by func_table_call
        # Clean old generated files before writing new ones
        if os.path.exists(output_dir):
            for f in os.listdir(output_dir):
                if f.startswith('recomp_') and f.endswith('.c'):
                    os.remove(os.path.join(output_dir, f))
        os.makedirs(output_dir, exist_ok=True)

    def generate_all(self):
        """Generate all recompiled C files."""
        functions = sorted(self.analyzer.functions.values(), key=lambda f: f['start'])

        # Split into chunks for manageable file sizes
        chunks = []
        for i in range(0, len(functions), self.MAX_FUNCS_PER_FILE):
            chunk = functions[i:i + self.MAX_FUNCS_PER_FILE]
            chunk_start = chunk[0]['start']
            chunk_end = chunk[-1]['end']
            chunk_name = f"recomp_{chunk_start:06X}_{chunk_end:06X}"
            chunks.append((chunk_name, chunk))

        # Generate each chunk (first pass — discovers cross-function targets)
        all_func_names = []
        for chunk_name, chunk_funcs in chunks:
            self._generate_chunk(chunk_name, chunk_funcs, functions)
            for f in chunk_funcs:
                all_func_names.append(f['name'])

        # Iteratively resolve cross-function call targets until stable.
        # Each pass may discover new targets from newly-split functions.
        for iteration in range(10):  # safety limit
            unregistered = self.cross_func_targets - set(f['start'] for f in functions)
            # Filter to valid ROM addresses
            unregistered = {a for a in unregistered if 0x200 <= a < self.rom.size and not (a & 1)}
            if not unregistered:
                break
            print(f"  Pass {iteration+1}: {len(unregistered)} cross-function targets — splitting and regenerating...")
            for addr in unregistered:
                if addr not in self.analyzer.labels:
                    self.analyzer.labels[addr] = f'loc_{addr:06X}'
            all_entries = set(f['start'] for f in functions) | unregistered
            self.analyzer._build_functions(all_entries)
            functions = sorted(self.analyzer.functions.values(), key=lambda f: f['start'])
            chunks = []
            for i in range(0, len(functions), self.MAX_FUNCS_PER_FILE):
                chunk = functions[i:i + self.MAX_FUNCS_PER_FILE]
                chunk_start = chunk[0]['start']
                chunk_end = chunk[-1]['end']
                chunk_name = f"recomp_{chunk_start:06X}_{chunk_end:06X}"
                chunks.append((chunk_name, chunk))
            for f_name in os.listdir(self.output_dir):
                if f_name.startswith('recomp_') and f_name.endswith('.c'):
                    os.remove(os.path.join(self.output_dir, f_name))
            self.cross_func_targets.clear()
            all_func_names = []
            for chunk_name, chunk_funcs in chunks:
                self._generate_chunk(chunk_name, chunk_funcs, functions)
                for f in chunk_funcs:
                    all_func_names.append(f['name'])
            print(f"    Now have {len(functions)} functions")

        # Generate registration header
        self._generate_registration(functions)

        # Skip main.c generation — hand-written src/main.c is used instead
        # self._generate_main(functions)

        print(f"\nGenerated {len(chunks)} source files with {len(functions)} functions")
        print(f"Output directory: {self.output_dir}")

    def _generate_chunk(self, chunk_name, funcs, all_functions=None):
        """Generate one C source file for a chunk of functions."""
        lines = []
        lines.append(f'/* Generated by genrecomp tools/recompiler from {self.title}. Do not commit. */')
        lines.append(f'/* Source range: ${funcs[0]["start"]:06X} - ${funcs[-1]["end"]:06X} */')
        lines.append(f'/* Functions: {len(funcs)} */')
        lines.append(f'')
        lines.append(f'#include <genrecomp/genrecomp.h>')
        lines.append(f'#include "recomp_funcs.h"')
        lines.append(f'')

        # Build a map from function start -> next function for fall-through
        next_func_map = {}
        if all_functions:
            for i in range(len(all_functions) - 1):
                next_func_map[all_functions[i]['start']] = all_functions[i + 1]

        regions = self._fallthrough_regions(all_functions or funcs)

        for func in funcs:
            region = regions[func['start']]
            next_func = next_func_map.get(region[-1]['start'])
            lines.extend(self._generate_function(func, next_func, region))
            lines.append('')

        path = os.path.join(self.output_dir, f'{chunk_name}.c')
        with open(path, 'w') as f:
            f.write('\n'.join(lines))

    @staticmethod
    def _fallthrough_regions(functions):
        """Group functions that run into each other into regions.

        The analyzer splits code at every externally-referenced address, so a
        loop whose head is such an address ends up spanning two C functions,
        and its back-edge becomes a recursive call that grows the C stack
        every iteration (see docs/debugging.md). Every function in a region
        is emitted with the whole region's code and enters it with a goto,
        so all branches inside the region stay gotos.
        """
        regions, cur = {}, []
        for f in functions:
            if cur:
                prev = cur[-1]
                last = prev['insn_addrs'][-1] if prev['insn_addrs'] else None
                mnem = CodeGenerator._mnem_of(prev, last)
                runs_on = (prev['end'] == f['start'] and not prev['has_return']
                           and not mnem.startswith(('bra', 'jmp')))
                if not runs_on:
                    for g in cur:
                        regions[g['start']] = cur
                    cur = []
            cur.append(f)
        for g in cur:
            regions[g['start']] = cur
        return regions

    _instructions = None

    @staticmethod
    def _mnem_of(func, addr):
        insns = CodeGenerator._instructions
        return insns[addr][0] if insns and addr in insns else ''

    def _generate_function(self, func, next_func=None, region=None):
        """Generate C code for one function (entered at func['start'])."""
        CodeGenerator._instructions = self.analyzer.instructions
        region = region or [func]
        lines = []
        name = func['name']
        start = func['start']
        end = func['end']
        reg_start, reg_end = region[0]['start'], region[-1]['end']
        insn_addrs = [a for g in region for a in g.get('insn_addrs', [])]

        lines.append(f'/* ${start:06X}-${end:06X}  ({func["insn_count"]} instructions, {func["size"]} bytes) */')
        if len(region) > 1:
            lines.append(f'/* region ${reg_start:06X}-${reg_end:06X}, entered at ${start:06X} */')
        lines.append(f'void {name}(void) {{')

        if not insn_addrs:
            lines.append(f'    /* empty function */')
            lines.append(f'}}')
            return lines

        # Every function start is a potential goto target within the region
        for g in region:
            self.analyzer.labels.setdefault(g['start'], g['name'])
        if start != reg_start:
            lines.append(f'    goto {self.analyzer.labels[start]};')

        for addr in insn_addrs:
            if addr not in self.analyzer.instructions:
                continue
            mnemonic, op_str, size, raw = self.analyzer.instructions[addr]
            c_lines = self.translator.translate_instruction(addr, mnemonic, op_str, raw, reg_start, reg_end)
            lines.extend(c_lines)

        # If function didn't end with RTS/RTE/JMP/BRA, it falls through to the
        # next function. Generate a call to the next function so execution continues.
        # Skip if the last emitted line already returns (e.g. func_table_call + return).
        func = region[-1]
        if not func['has_return'] and next_func:
            # Check if the last line is an unconditional return/jump.
            # Conditional returns (inside if) still need fall-through.
            last_code = lines[-1].strip() if lines else ''
            needs_fallthrough = True
            if last_code == 'return;' or last_code == 'return; /* RTE */':
                needs_fallthrough = False
            elif '{ func_table_call(' in last_code and 'return; }' in last_code and not last_code.startswith('if'):
                needs_fallthrough = False
            if needs_fallthrough:
                next_name = next_func['name']
                lines.append(f'    /* Fall through to next function */')
                lines.append(f'    func_table_tail(0x{next_func["start"]:06X}); /* {next_name} */')

        lines.append(f'}}')

        # Post-process: fix cross-function gotos
        # Collect all labels defined in this function
        defined_labels = set()
        for line in lines:
            stripped = line.strip()
            if stripped.endswith(':') and not stripped.startswith('/*') and not stripped.startswith('//'):
                label_name = stripped[:-1]
                defined_labels.add(label_name)

        # Replace gotos to undefined labels with func_table_call
        fixed_lines = []
        for line in lines:
            m = re.search(r'goto ((?:loc|sub|jt|vec|main_game_entry)_?([0-9A-Fa-f]+));', line)
            if m and m.group(1) not in defined_labels:
                addr_val = int(m.group(2), 16)
                self.cross_func_targets.add(addr_val)
                line = line.replace(
                    f'goto {m.group(1)};',
                    f'{{ func_table_call(0x{addr_val:06X}); return; }}'
                )
            fixed_lines.append(line)

        # Jumps out of the function are tail calls: hand them to the
        # dispatcher's trampoline so M68K loops through JMP/BRA chains don't
        # grow the C stack (genrecomp func_table_tail).
        fixed_lines = [re.sub(r'func_table_call\(([^;]*)\); return;', r'func_table_tail(\1); return;', l)
                       for l in fixed_lines]

        # Drop labels nothing jumps to (every instruction address with a
        # label gets one, which would otherwise trip unused-label warnings)
        used = set(re.findall(r'goto (\w+);', '\n'.join(fixed_lines)))
        fixed_lines = [l for l in fixed_lines
                       if not (re.fullmatch(r'\w+:', l.strip()) and l.strip()[:-1] not in used)]

        # Also track all func_table_call targets already in the code
        # (from translator's _gen_bsr, _gen_bcc, etc.)
        for line in fixed_lines:
            for m in re.finditer(r'func_table_(?:call|tail)\(0x([0-9A-Fa-f]+)\)', line):
                self.cross_func_targets.add(int(m.group(1), 16))

        return fixed_lines

    def _generate_registration(self, functions):
        """Generate the header with forward declarations and registration function."""
        lines = []
        lines.append('/* Auto-generated — forward declarations + registration */')
        lines.append('#ifndef RECOMP_FUNCS_H')
        lines.append('#define RECOMP_FUNCS_H')
        lines.append('')
        lines.append('#include <genrecomp/genrecomp.h>')
        lines.append('')
        # MSVC-compatible helpers for post-increment/pre-decrement addressing modes
        lines.append('/* Post-increment read: read from (An) then An += size */')
        for sz in [8, 16, 32]:
            inc = sz // 8
            step = '(n == 7 ? 2 : 1)' if sz == 8 else str(inc)   # byte (A7)+ moves SP by 2
            lines.append(f'static inline uint{sz}_t _postinc{sz}(int n) {{ uint{sz}_t v = bus_read{sz}(g_m68k.a[n]); g_m68k.a[n] += {step}; return v; }}')
        lines.append('/* Pre-decrement read: An -= size then read from (An) */')
        for sz in [8, 16, 32]:
            dec = sz // 8
            step = '(n == 7 ? 2 : 1)' if sz == 8 else str(dec)   # byte -(A7) moves SP by 2
            lines.append(f'static inline uint{sz}_t _predec{sz}(int n) {{ g_m68k.a[n] -= {step}; return bus_read{sz}(g_m68k.a[n]); }}')
        lines.append('')

        # Forward declarations
        for func in functions:
            lines.append(f'void {func["name"]}(void);')
        lines.append('')

        # Registration function
        lines.append('static inline void recomp_register_all(void) {')
        for func in functions:
            lines.append(f'    func_table_register(0x{func["start"]:06X}, {func["name"]});')
        lines.append('}')
        lines.append('')
        lines.append('#endif /* RECOMP_FUNCS_H */')

        path = os.path.join(self.output_dir, 'recomp_funcs.h')
        with open(path, 'w') as f:
            f.write('\n'.join(lines))


def main():
    import argparse
    parser = argparse.ArgumentParser(description='genrecomp 68K recompiler')
    parser.add_argument('rom', help='Path to Genesis ROM file')
    parser.add_argument('--output-dir', '-o', default='src/recomp', help='Output directory for generated C files')
    parser.add_argument('--config', '-c', help='Title config JSON (title, seeds, code_ranges)')
    args = parser.parse_args()

    import json
    cfg = json.load(open(args.config)) if args.config else {}
    print(f"=== genrecomp recompiler: {cfg.get('title', args.rom)} ===\n")

    rom = GenesisROM(args.rom)
    rom.print_info()

    print("\n--- Phase 1: Analysis ---")
    analyzer = M68KAnalyzer(rom)
    analyzer.load_config(cfg)
    analyzer.analyze()

    # Phase 1.5: discover additional entry points from cross-function calls
    # that reference addresses not in any discovered function
    extra_entries = set()
    for func in analyzer.functions.values():
        for call_addr in func['calls']:
            if call_addr not in analyzer.functions and 0x200 <= call_addr < rom.size:
                extra_entries.add(call_addr)
    # Also scan generated code patterns for func_table_call references
    for addr, (mnemonic, op_str, size, raw) in analyzer.instructions.items():
        if mnemonic in ('jmp', 'jsr', 'bra', 'bsr'):
            target = analyzer._extract_branch_target(
                type('obj', (), {'op_str': op_str, 'operands': [], 'mnemonic': mnemonic})(),
                addr
            )
            if target and target not in analyzer.functions and 0x200 <= target < rom.size and not (target & 1):
                extra_entries.add(target)

    if extra_entries:
        print(f"\n--- Phase 1.5: Discovering {len(extra_entries)} additional functions ---")
        work = list(extra_entries)
        while work:
            new_work = []
            for addr in work:
                if addr in analyzer.visited or addr >= rom.size or addr < 0x200 or (addr & 1):
                    continue
                new_targets = analyzer._disassemble_block(addr)
                for target, is_call in new_targets:
                    if target not in analyzer.visited and 0x200 <= target < rom.size and not (target & 1):
                        new_work.append(target)
                        if is_call:
                            extra_entries.add(target)
            work = new_work
        analyzer._build_functions(extra_entries | set(analyzer.functions.keys()))
        print(f"Now have {len(analyzer.functions)} functions, {len(analyzer.instructions)} instructions")

    print("\n--- Phase 2: Code Generation ---")
    translator = M68KTranslator(rom, analyzer.labels)
    translator.pc_switches = analyzer.pc_switches
    generator = CodeGenerator(rom, analyzer, translator, args.output_dir)
    generator.title = cfg.get('title', rom.title_overseas or 'a Genesis ROM')
    generator.generate_all()

    print("\nDone!")


if __name__ == '__main__':
    main()
