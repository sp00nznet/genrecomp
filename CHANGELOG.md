# Changelog

All notable changes to genrecomp. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [SemVer](https://semver.org/).

## [Unreleased]

### Added
- `--headless`, `--record out.mp4`, `--frames N`, `--press F:BTN[:LEN]` and
  `--ram-dump DIR[:N]` for every title via `platform_parse_args()`. (#2)
- `genrecomp_ref`: the reference runner, which plays a ROM on GPGX's 68K
  interpreter with the same flags, as ground truth to diff a recomp against. (#2)
- `func_table_dump_stack()`: a shadow call stack of 68K addresses. (#2)
- Scanline clock: bus accesses advance time, each line runs the Z80, VBlank
  calls an optional title callback, and each frame mixes audio. (#3)
- `func_table_tail()`: a tail-jump trampoline, so looping JMP/BRA chains don't
  grow the C stack. (#4)
- `tests/test_runtime.c`, run by `ctest`. (#1, #4)
- `docs/recomp-runtime.md`, `NOTICE`, `CONTRIBUTING.md`, `ROADMAP.md`, CI,
  and `Setup.cmd` / `setup.sh`. (#5)
- `func_table_jsr` / `func_table_rts`: calls push real return addresses and
  RTS follows the 68K stack, including into pushed continuations. (#6)
- `recomp_m68k_interrupt` / `genrecomp_vblank_irq`: interrupts with the
  full context restored, SR masking and VDP IE0. (#6)

### Fixed
- 8/16-bit ALU macros cleared the upper bits of the destination register. (#1)
- Large VDP DMAs never completed, and the VDP dropped writes behind them. (#3)
- Stale FIFO timestamps and unmapped accesses skipped hundreds of frames. (#3)
- No controller input reached games: `input.system` was set after
  `system_init()`. (#3)
- Mid-frame H32/H40 switches were ignored. (#3)
- Stale pixels were left at the right edge after switching from H40 to H32. (#5)
- Interrupts clobbered the interrupted code's flags and ran while masked. (#6)

### Removed
- Unused VDP/YM2612/PSG/Z80 wrapper modules, the debug framework and its
  GDB "stub" (which only printed), the debug runner, and the unused ymfm
  submodule. (#5)
- `io_read`, `io_write`, `genrecomp_io_init` and `genrecomp_io_shutdown`.
  Recompiled code reaches the I/O chip through the bus. (#5)

## Initial implementation - 2026-03-15 (untagged)

M68K CPU context, bus over the GPGX memory map, function table, SDL2
platform layer.
