# genrecomp architecture

genrecomp is the hardware side of a Genesis static recompilation. A title's
generator turns 68K machine code into C; genrecomp supplies everything that
code talks to. It is a static library over Genesis Plus GX (GPGX), which
provides the VDP, YM2612, PSG, Z80 and I/O chip. Nothing in a title build runs
GPGX's 68K interpreter.

```
  title: generated C (one function per 68K entry point) + main.c
     │  g_m68k registers, bus_read/write, func_table_call/tail
  ┌──▼──────────────────────────────────────────────────────────┐
  │ genrecomp                                                    │
  │  m68k.h      register file, flag and ALU macros              │
  │  bus.c       24-bit bus -> GPGX memory map; scanline clock   │
  │  func_table  address -> function dispatch, tail trampoline   │
  │  genrecomp.c ROM load, per-frame render and present          │
  │  platform    SDL2 window/audio, or headless + ffmpeg record  │
  │  input, io   keyboard / scripted presses -> GPGX pads        │
  └──▲──────────────────────────────────────────────────────────┘
  ┌──┴──────────────────────────────────────────────────────────┐
  │ Genesis Plus GX (ext/, submodule): VDP, FM, PSG, Z80, I/O    │
  └─────────────────────────────────────────────────────────────┘
```

## Who owns what

| Part | Owns | File |
|---|---|---|
| CPU state | `g_m68k`: D0-D7, A0-A7, SR flags, USP/SSP. Recompiled code reads and writes it directly. | `include/genrecomp/m68k.h`, `src/m68k.c` |
| Bus | Every memory access. Routes through GPGX's `m68k.memory_map[]`, so RAM, ROM, VDP ports, I/O and Z80 space behave exactly as in GPGX. | `src/bus.c` |
| Clock | Time. Each access costs 28 master cycles; whole scanlines run the Z80, VBlank calls the title's callback, frame ends mix audio and rebase counters. | `src/bus.c`, [recomp-runtime.md](recomp-runtime.md) |
| Dispatch | Calls by 68K address: JSR/BSR, indirect calls, exception vectors, and tail jumps. | `src/func_table.c` |
| Frame | Rendering all scanlines once per frame and presenting them. | `src/genrecomp.c` |
| Platform | SDL2 window and audio, or headless: no window, frames piped to ffmpeg, scripted input. | `src/platform_sdl.c`, `src/input.c`, `src/io.c` |
| GPGX config | Options GPGX reads (region, sound, ROM loading). | `src/osd.c` |

## Data flow in a frame

1. The title's recompiled main loop runs; every `bus_*` call advances the clock.
2. When the clock enters VBlank (line 224), the title's VBlank callback runs:
   it polls input, calls the game's own VBlank handler through
   `func_table_call`, and calls `genrecomp_end_frame()` to render and present.
3. The clock carries on through the VBlank lines. At the last line it mixes
   audio and rebases all cycle counters for the next frame.

The main loop never returns in most titles (Pigskin waits in a TRAP), so the
frame is driven from inside bus accesses, not from an outer loop.

## Why these choices

- **GPGX for hardware** because it is accurate and its chips are separable
  from its CPU core. The cost is its licence: non-commercial (see
  [NOTICE](../NOTICE)), which every binary built with genrecomp inherits.
- **A global CPU context** so recompiled functions can be `void f(void)`:
  the generator never has to thread state through calls.
- **Big-endian bus semantics.** `bus_read16` returns the 68K's view. GPGX
  stores work RAM byte-swapped on little-endian hosts; `bus.c` hides that,
  and `--ram-dump` files keep GPGX's layout (XOR an address with 1).
- **Everything a title calls goes through `func_table_call`**, including its
  entry point and VBlank handler, so tail jumps (`func_table_tail`) and the
  shadow call stack stay consistent.
