# The 68K recompiler (`tools/recompiler/`)

`generate.py` turns a Genesis ROM into C for genrecomp: one C function per
discovered entry point, every instruction a statement on `g_m68k` and the bus.
`analyze.py` does the discovery. Both need Python 3.10+ and `capstone`.

```
python tools/recompiler/generate.py rom.gen -o src/recomp -c recomp.json
```

The output is derived from the ROM, so it belongs in the title's gitignored
build tree and never in a repository. What a title commits is its
`recomp.json`: addresses and table descriptions, no code.

It started as Pigskin Footbrawl's generator and was generalised to get
General Chaos in game. Both titles build from it unchanged.

## Title config (`recomp.json`)

```json
{
  "title": "General Chaos",
  "seeds": { "0x0012E8": "sub_0012E8" },
  "offset_tables": [
    { "comment": "sound command handlers, dispatched at $0DFA2A",
      "table": "0x0DFA50", "base": "0x0DF2A4", "size": 4, "count": 108 }
  ]
}
```

| Key | Meaning |
|---|---|
| `title` | Printed in generated headers. |
| `code_ranges` | Where the heuristic scans may look for code (default: all of the ROM). Pigskin's code is all above `$0E0000`; the rest is data, and scanning it invents functions. |
| `seeds` | Entry points nothing else reveals: targets of computed jumps, pointers kept in data. |
| `offset_tables` | Dispatch tables: `count` signed entries of `size` bytes at `table`, each an offset from `base` (0 = absolute). Every target becomes an entry point. |

## Finding code (`analyze.py`)

1. **Descent** from the reset vector, every interrupt and TRAP vector, and the
   seeds, following branches, calls (including PC-relative `jsr $1826(pc)`)
   and compiled `switch` tables (below).
2. **Heuristic scans**, each followed by more descent: longword address
   tables, address loads (`lea`, `pea`, `move.l #`, absolute or PC-relative),
   and function prologues (`link`, `movem.l ...,-(sp)`). They only look at
   bytes descent didn't decode, inside `code_ranges`.
3. **Overlap resolution.** Two decoded instructions that overlap can't both be
   code. Each instruction gets the rank of the best source that reaches it
   through control flow (0 for vectors and seeds, then one per scan), and the
   worse one is dropped with any function entry on it. Without this, an
   "address" found in a table landed inside a `muls.w #6,d0`, turned the
   multiply into garbage, and a loop count overran a stack buffer (General
   Chaos, `$001870`).

Capstone reports sized mnemonics (`bra.b`, `bsr.w`, `lea.l`, `link.w`).
Compare on `mnemonic.split('.')[0]`; several early bugs were an exact
comparison against `'bra'` or `'lea'` silently never matching.

## Emitting C (`generate.py`)

- **Fall-through regions.** Functions that run into each other form a region.
  Every entry point is emitted with the whole region's code and starts with a
  `goto` to its own label, so every branch inside the region is a `goto`.
  Loops whose head is some other function's entry would otherwise be
  recursive C calls.
- **Calls** are `func_table_jsr(target, return_address)`: the real return
  address is pushed on the 68K stack. **RTS** is `func_table_rts()`, which pops
  it and follows it if the code arranged its own continuation. See
  `recomp-runtime.md`.
- **Jumps out of a region** (BRA, JMP, fall-through) are tail jumps,
  `{ func_table_tail(x); return; }`, so looping jump chains don't grow the C
  stack.
- **Compiled switches** (`move.w T(pc,dN.l),dN; jmp T(pc,dN.w)`, a table of word
  offsets from T right after the JMP) become a C `switch` with a `goto` per
  case. Cases outside the region become tail jumps to registered entries.
- **68000 details that bit:** byte-sized `-(a7)`/`(a7)+` move SP by 2; MOVEM
  with the address register in its own list stores its initial value
  (`-(An)`) and ignores the loaded one (`(An)+`); `.b`/`.w` ALU operations keep
  the register's upper bits; ABCD/SBCD/NBCD have macros in `m68k.h`.
- **Not translated:** data that descent decodes as code (`dc.w`, usually
  unreachable). Each is emitted as a `/* TODO $addr: ... */` comment; count
  them with `grep -c TODO src/recomp/*.c`.

## Getting a new title to run

The loop that got General Chaos from black screen to battles:

1. Generate with an empty config, build, and run headless beside the reference
   runner with the same `--press` script (`recomp-runtime.md`, Tooling).
2. **Dispatch misses** (`func_table: no function at $0E053E ... call stack`)
   are entry points the analyzer didn't find. Even ROM addresses become
   `seeds`. If many come through one dispatcher, find its table and add an
   `offset_tables` entry instead. Odd or huge addresses are fallout from an
   earlier error; fix that first.
3. **`RTS to $000000`** means a return address was overwritten. Find the
   writer with `GENRECOMP_WATCH=FFFEE1` (any hex address), which logs every
   write to that byte with the shadow call stack. In General Chaos that was
   the `muls` decoded as garbage (step 3 of Finding code).
4. **The game runs but decides differently** (skips a menu, takes another
   path): dump RAM from both runners (`--ram-dump`), find the variable the
   decision reads, and trace back to where it diverges. A game's timers can
   look fast only because loading takes fewer frames in the recomp; compare
   frame counters, not screens.
5. Unit-test a suspect routine in isolation: call it with
   `func_table_jsr(addr, 0)` from `main` on a guarded buffer, before the game
   starts. That separated a correct memset, modulo and qsort from the real
   bug.
