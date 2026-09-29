# Roadmap

## Next

- **Hear the audio.** The Z80 and sound chips run and mix every frame, but
  nobody has listened to a windowed run yet, and `--record` writes video only.
  Add the audio track to `--record`.
- **Conformance harness (house style §9).** The reference runner is the
  ground truth. A harness should run each title's scripted input on both
  runners, compare RAM and VDP state at checkpoints, and report a pass/fail
  count in CI and in this README. The ROMs can't be redistributed, so it
  skips with a message when they are absent.
- **Fewer seeds.** General Chaos still needs ~15 hand-found entry points and one
  declared dispatch table. Base-plus-offset tables (`lea base(pc); lea
  table(pc); ...; adda; jsr (a0)`) could be recognised like compiled
  switches are.
- **Return points as entry points.** Task switchers return into another
  task's saved PC mid-function; those RTSes fall back to a plain return
  today. Making every call's return address an entry point would handle
  them, at a large cost in code size.
- **H-interrupts.** The clock doesn't raise level-4 interrupts, so raster
  splits (Pigskin's title screen) render wrong.

## Deferred

- Timing-exact DMA and mid-frame raster effects that depend on it. DMA
  completes instantly today (see `docs/recomp-runtime.md`).
- Recompiling the Z80. It runs on GPGX's Z80 core; it only drives audio.
- Linux and macOS as tested platforms. CI builds on Linux; no one has
  played a title there.

## Out of scope

- A 68K interpreter fallback in title builds. The reference runner uses
  GPGX's interpreter for comparison only; titles run recompiled code only.
- Sega CD, 32X and Master System modes, although GPGX supports them.
- Commercial use. Genesis Plus GX's licence forbids it (see `NOTICE`).
