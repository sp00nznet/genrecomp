# Running recompiled code on GPGX hardware

Recompiled 68K code never runs `m68k_run()` or `system_frame_gen()`, but every
piece of Genesis Plus GX hardware it talks to was written assuming both do. This
file is the list of places where that assumption broke a real title, what
genrecomp does instead, and where the code is. Every item here was found on
Pigskin Footbrawl (sp00nznet/pigskin) by diffing against the reference runner.

## The clock (`src/bus.c`, `bus_tick_cycles`)

Each bus access adds 28 master cycles to `m68k.cycles` (about one 68K memory
cycle). Whenever a scanline's worth (3420) has accumulated, the line advances,
mirroring `system_frame_gen()`:

- the Z80 runs to the end of the line (`z80_run`), so a sound driver drains the
  68K's mailbox and plays;
- at the first VBlank line the VBlank flag is set, the Z80 IRQ is raised for
  one line, and the recomp's VBlank callback runs;
- at the end of the frame the sound chips are mixed (`audio_update`) and every
  counter is rebased: `m68k.cycles`, `Z80.cycles`, `mcycles_vdp`,
  `dma_endCycles`, `fifo_cycles[]`, and `input_end_frame()`.

Rebasing, not zeroing, matters: GPGX stores absolute timestamps. A stale
`fifo_cycles[]` entry made one VDP data write stall the CPU "until" a timestamp
hundreds of lines ahead, and hundreds of frames went by inside that one write.

**Time never runs backwards.** An access to an unmapped address makes GPGX
"lock up" the CPU with `m68k.cycles = m68k.cycle_end`, which is 0 here.
`cycles - mcycles_vdp` is unsigned, so it wrapped and the line loop raced
through thousands of frames. The tick clamps and warns once per occurrence:

```
bus: cycle counter went backwards (last access $BC8F08) -- unmapped access?
```

That warning means the recompiled code built a bad pointer; real hardware would
hang there. Chase it with the reference runner.

## DMA completes instantly (`bus_finish_dma`)

`vdp_dma_update()` moves one line's worth of a DMA; `system_frame_gen()` does
the rest. Without it a large DMA stays pending forever, and while a 68K-bus DMA
is pending GPGX latches further control-port writes and drops them, so the VDP
wedges. After every VDP write genrecomp runs the DMA to completion and restores
`m68k.cycles` (a 68K-bus DMA would stall the CPU; recompiled code doesn't).
Mid-frame raster effects fed by DMA timing are out of scope.

## Display width (`genrecomp_end_frame`)

GPGX defers an H32/H40 switch made during active display to the next frame
boundary inside `system_frame_gen()`. genrecomp renders the whole frame at the
end, so it takes the width from VDP register 12 at that point.

## Input (`genrecomp_load_rom`)

`input.system[]` must be set *before* `system_init()`: its `io_init()` binds the
port handlers from it. Set afterwards, both ports stay on dummy handlers and no
button ever reaches the game, in the recomp and the reference alike.

## Sized ALU macros (`include/genrecomp/m68k.h`)

`.b` and `.w` operations on a data register change only its low byte or word.
Every 8/16-bit macro used to write the full 32 bits back, so `ror.w #8,d1;
swap d1` lost d1's upper word; in Pigskin that sent a sprite-table DMA to ROM
instead of RAM and the screen went black after the SEGA logo. The macros now
merge: `(dst) = ((dst) & ~0xFFFFu) | (uint16_t)result`, which is also correct
for the sized temporaries generated for memory operands.

## Tail jumps (`src/func_table.c`, `func_table_tail`)

68K code jumps between routines with `JMP`/`BRA` and loops through such chains
indefinitely. Emitted as `func_table_call(x); return;` each jump nests a C call,
and a loop through two routines grows the native stack every iteration until the
depth guard aborts (depth 501 after ~10 seconds of Pigskin gameplay). Generated
code should emit `{ func_table_tail(x); return; }`: the dispatcher runs the
target after the current function returns, in the same C frame. Anything a game
calls directly rather than through `func_table_call` (its entry point, its
VBlank handler) must go through the dispatcher too, or a pending tail jump is
never taken.

## Tooling

All take the same flags (`platform_parse_args`, `include/genrecomp/platform.h`):

| Flag | Effect |
|---|---|
| `--headless` | No window, no audio device, no frame pacing. Works over RDP. |
| `--record out.mp4` | Pipe every presented frame to ffmpeg (must be on PATH). |
| `--frames N` | Exit after N presented frames. |
| `--press F:BTN[:LEN]` | Hold `BTN` (`START`, `A+RIGHT`, ...) from frame F for LEN frames (default 6). |
| `--ram-dump DIR[:N]` | Every N frames write RAM + VDP regs/CRAM/VSRAM/VRAM to `DIR/ram_<frame>.bin`. |

`genrecomp_ref` (`tools/ref_runner/main.c`) plays the ROM on GPGX's own 68K
interpreter with the same flags. It is the ground truth:

```
genrecomp_ref --headless --frames 1600 --ram-dump ref:10 rom.gen
pigskin       --headless --frames 1600 --ram-dump rc:10  rom.gen
```

Diff the two dumps and the first frame they disagree on is where the recompiled
code went wrong. The recomp runs a little ahead of the reference (its cycle
model is approximate), so align by content, not by frame number.
`func_table_dump_stack()` prints the shadow call stack; called from the VBlank
callback it shows where the game's main thread is blocked.

`tests/test_runtime.c` checks the macro-width and tail-jump behaviour
(`ctest -C Release` from the build directory).
