# genrecomp Architecture

## Overview

genrecomp provides Genesis/Mega Drive hardware as a linkable static library for static
recompilation projects. The architecture follows the same pattern as snesrecomp: a thin
adapter layer wraps a real emulator core (Genesis Plus GX) and exposes it through a
clean C API.

## Layer Diagram

```
┌─────────────────────────────────────────────────────┐
│                  Your Recompiled Game                │
│         (M68K → C, compiled to native x86)          │
├─────────────────────────────────────────────────────┤
│                  genrecomp API                       │
│  M68kContext │ Bus │ FuncTable │ Platform │ Input    │
├─────────────────────────────────────────────────────┤
│              genrecomp Adapters                      │
│  VDP │ YM2612 │ PSG │ Z80 │ I/O │ Debug            │
├─────────────────────────────────────────────────────┤
│            Genesis Plus GX Core                      │
│  VDP rendering │ FM/PSG synthesis │ Z80 CPU │ Memory│
├─────────────────────────────────────────────────────┤
│               SDL2 Platform Layer                    │
│  Window │ Framebuffer │ Audio │ Input │ Timing      │
└─────────────────────────────────────────────────────┘
```

## Key Design Decisions

### The CPU Is NOT Emulated

The M68K CPU is replaced entirely by recompiled native C code. `M68kContext` holds the
register state (D0-D7, A0-A7, flags, etc.) and recompiled instructions read/write it
directly. There is no instruction decoder, no interpreter loop.

### Big-Endian Bus

The Genesis M68K is big-endian. All bus operations preserve this byte ordering.
`bus_read16(addr)` returns `(mem[addr] << 8) | mem[addr+1]`. This matches how
recompiled code expects memory to behave.

### Global State

Following snesrecomp's pattern, the CPU context is a global (`g_m68k`). This enables
recompiled functions to be simple `void(*)(void)` with no arguments — they just read
and write the global state and call bus functions.

### Function Dispatch

The function table maps original M68K addresses to native function pointers. This handles:
- Direct calls (BSR/JSR to known addresses)
- Indirect calls (JSR to computed addresses — looked up at runtime)
- Interrupt handlers (vector table entries)

### Phase-Based Hardware Integration

Phase 1 provides the CPU context, bus (ROM + RAM only), and function table. Hardware
stubs (VDP, sound, Z80, I/O) return sensible defaults. Phase 2 wires everything through
Genesis Plus GX for full hardware accuracy.
