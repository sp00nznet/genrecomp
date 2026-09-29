# Contributing to genrecomp

genrecomp is the Genesis toolkit in the sp00nznet recomp family (ps3recomp,
xboxrecomp, snesrecomp, lynxrecomp, ...). It shares their house style: the same
CLI flags, a headless mode, and no game data in the repo.

## Setup

Run `Setup.cmd` (Windows) or `setup.sh` (Linux), or follow Getting Started in the
[README](README.md). Before opening a PR:

```
cmake --build build --config Release
cd build && ctest -C Release
```

## What helps most

- **Try it on a new title** and report where it stops. The most useful report
  is a pair of `--record` videos, one from your recomp and one from
  `genrecomp_ref` with the same `--press` script, plus the frame where they
  diverge.
- **Fixes found on a title.** Describe the behaviour generically ("DMA longer
  than one scanline never completes"), say which title hit it, and keep
  anything title-specific in the title's repo. The runtime gets the general
  mechanism: a hook, a flag or a callback.
- **Hardware notes.** When something takes a day to work out, add it to
  `docs/recomp-runtime.md`.

## Code style

- C17. It must build with MSVC 2022; GCC and Clang are checked in CI.
- 4-space indentation. Comments explain why, not what.
- Don't change behaviour for every title without saying so in the PR. If it
  can't be the default, make it opt-in.

## Game data

Never commit ROMs, dumps, generated source, RAM dumps or recordings of
copyrighted audio. Tests must run without game data, or skip with a message
that says what is missing.

## Where your code comes from

Contributions must be your own work, or come from a source whose licence is
compatible with MIT (MIT, BSD, zlib, Apache-2.0, public domain).

- **Don't port code from GPL projects.** For the Genesis that means
  BlastEm, MAME's GPL drivers, Kega-derived code, PicoDrive's GPL parts, and
  other recompilers under the GPL. Rewriting line by line doesn't change that.
  Reading one to understand the hardware is fine; carrying its code or
  structure across is not.
- **Genesis Plus GX** is a dependency, used unmodified as a submodule under
  its own non-commercial licence (see `NOTICE`). Don't copy its code into
  `src/`.
- **If something came from elsewhere, say so in the PR**, with a link, and
  keep any copyright header.
- **AI-assisted contributions are welcome**, provided a human understood and
  verified the change. If generated code looks like it reproduces an existing
  project, check where it came from before submitting it.

## Where to ask

[The sp00nznet recomp Discord](https://discord.gg/CRpzGWZFcu).

## Licence

MIT. By submitting a contribution you agree that it is released under the
same licence.
