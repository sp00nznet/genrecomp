# genrecomp — Sega Genesis Static Recompilation Toolkit

```

    ____  _____ _   _ ____  _____ ____ ___  __  __ ____
   / ___|| ____| \ | |  _ \| ____/ ___/ _ \|  \/  |  _ \
  | |  _ |  _| |  \| | |_) |  _|| |  | | | | |\/| | |_) |
  | |_| || |___| |\  |  _ <| |__| |__| |_| | |  | |  __/
   \____||_____|_| \_|_| \_\_____\____\___/|_|  |_|_|

   Bring Genesis games to life on modern hardware.
   No emulator. Just your favorite games, running natively.
```

**An open toolkit for statically recompiling Sega Genesis / Mega Drive (M68K) games to native x86-64.**

Ever wanted to take a Genesis classic and run it natively on your PC at full speed, with
modern resolution support and zero emulation overhead? That's what static recompilation does —
and this toolkit gives you everything you need to make it happen.

## Implementation Progress

| Phase | Component | Status |
|-------|-----------|--------|
| **1** | M68K CPU context (registers, flags, all macros) | Done |
| **1** | Memory bus (flat 24-bit, big-endian) | Done |
| **1** | Function dispatch table (hash table) | Done |
| **1** | Top-level API (init/load/frame/shutdown) | Done |
| **1** | SDL2 platform layer (window, audio, input, vsync) | Done |
| **1** | Input mapping (keyboard → Genesis 3/6-button) | Done |
| **1** | Debug framework (tracing, breakpoints, watchpoints) | Done |
| **2** | Genesis Plus GX integration (submodule + CMake) | Done |
| **2** | Bus routing through GenPlusGX memory map | Done |
| **2** | VDP adapter (wraps GenPlusGX VDP rendering) | Done |
| **2** | YM2612 FM adapter (wraps GenPlusGX sound) | Done |
| **2** | SN76489 PSG adapter | Done |
| **2** | Z80 coprocessor bridge | Done |
| **2** | I/O controller adapter (wraps GenPlusGX io_ctrl) | Done |
| **2** | OSD layer (config, ROM loading, input bridge) | Done |
| **2** | ymfm submodule (alternative FM backend) | Submodule added |
| **3** | Per-scanline VDP timing in frame loop | In Progress |
| **3** | Z80 sound driver execution per frame | In Progress |
| **3** | Audio mixing pipeline (FM + PSG → SDL2) | In Progress |
| **4** | GDB remote serial protocol stub | Planned |
| **4** | Comparison mode (recomp vs interpreter) | Planned |
| **5** | GitHub Actions CI | Planned |

## What Is This?

genrecomp is a **reusable library** for building Genesis static recompilation projects. It provides:

- **An M68K CPU context** with complete register file, flag handling, and all arithmetic/shift/rotate macros
- **A complete runtime library** that replaces the Genesis's hardware at the register level
- **VDP graphics** (Genesis video → SDL2 framebuffer) via Genesis Plus GX
- **YM2612 FM + SN76489 PSG audio** via Genesis Plus GX (with optional ymfm backend)
- **Z80 coprocessor** for sound driver execution
- **Input mapping** (keyboard/gamepad → Genesis 3/6-button pad)
- **Debug framework** with tracing, breakpoints, watchpoints, and GDB remote stub

Think of it like [N64Recomp](https://github.com/N64Recomp/N64Recomp) but for Genesis, or
like [snesrecomp](https://github.com/sp00nznet/snesrecomp) but for the other side of the
16-bit console war.

## How Static Recompilation Works

Unlike an emulator that interprets instructions at runtime, static recompilation translates
the **entire game binary ahead of time** into compilable C source code:

```c
// Original Genesis M68K:
//   MOVE.W  D0, $FF1234     // Store D0 to RAM
//   ADD.W   #1, D0          // Increment
//   BSR     update_score    // Call subroutine
//   BEQ     .skip           // Branch if zero

// Recompiled native C:
bus_write16(0xFF1234, (uint16_t)g_m68k.d[0]);
M68K_ADD16(g_m68k.d[0], 1);
func_table_call(0x001234);    // update_score
if (M68K_CC_EQ) goto skip;
```

The generated code compiles with any modern C compiler (MSVC, Clang, GCC) and runs at
**full native speed** with all compiler optimizations applied. No interpreter loop. No
JIT compilation. Just straight native code.

The trick is that games don't just run on a CPU — they talk to hardware. The Genesis has
a VDP for graphics, YM2612 + PSG for sound, a Z80 coprocessor, and controller I/O.
genrecomp provides **native replacements for all of these**, powered by Genesis Plus GX,
so the recompiled game code has everything it needs to run.

## Architecture

```
                    +-----------------+
                    | Your Genesis    |
                    |   ROM (.md)     |
                    | (Motorola 68K)  |
                    +--------+--------+
                             |
                    +--------v--------+
                    |  M68K → C       |
                    |  Recompiler     |
                    +--------+--------+
                             |
              +--------------+--------------+
              |              |              |
     +--------v------+ +----v----+ +-------v------+
     | genrecomp     | | genrecomp| | genrecomp   |
     | VDP Runtime   | | YM2612  | | Input        |
     | (SDL2)        | | + PSG   | | (KB/Gamepad  |
     |               | | Audio   | |  → Genesis)  |
     +-------+-------+ +----+----+ +------+-------+
             |               |             |
     +-------v---------------v-------------v-------+
     |              genrecomp Runtime               |
     |   M68kContext + Bus + Z80 + FuncTable        |
     +---------------------------------------------+
              Powered by Genesis Plus GX
```

## Getting Started

### Prerequisites

- **Windows 11** (Linux/macOS should work too)
- **CMake** 3.16+
- **SDL2** development libraries (vcpkg: `vcpkg install sdl2:x64-windows`)
- **Visual Studio 2022** or compatible C17 compiler

### Building

```bash
# Clone with submodules
git clone --recursive https://github.com/sp00nznet/genrecomp.git
cd genrecomp

# If you already cloned without --recursive:
git submodule update --init --recursive

# Configure (with vcpkg)
cmake -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build the library + minimal example
cmake --build build --config Release
```

### Build Outputs

| Target | Description |
|--------|-------------|
| `genplusgx_hw.lib` | Genesis Plus GX hardware backend (static library) |
| `genrecomp.lib` | genrecomp adapter library (static library) |
| `genrecomp_minimal.exe` | Minimal example showing API usage |
| `genrecomp_debug_runner.exe` | Debug runner with GDB stub (opt-in) |

### Using genrecomp in Your Project

genrecomp is designed to be used as a library. Your game-specific project links against it:

```cmake
# In your game project's CMakeLists.txt:
add_subdirectory(path/to/genrecomp)
target_link_libraries(my_game PRIVATE genrecomp SDL2::SDL2main)
```

### Quick Example

```c
#include <genrecomp/genrecomp.h>

int main(void) {
    genrecomp_init("My Genesis Recomp", 3);
    genrecomp_load_rom("game.md");

    // Register recompiled functions at their original M68K addresses
    func_table_register(0x000200, my_main_loop);
    func_table_register(0x001000, my_vblank_handler);

    while (genrecomp_begin_frame()) {
        func_table_call(0x000200);          // Run recompiled game frame
        genrecomp_trigger_vblank();          // VBlank processing
        genrecomp_end_frame();               // Render + present + sync
    }

    genrecomp_shutdown();
    return 0;
}
```

## Components

### M68K CPU Context (`include/genrecomp/m68k.h`)

The heart of the recompilation — a complete M68K register file with:

- **D0-D7** data registers, **A0-A7** address registers, **USP/SSP** stack pointers
- Individual boolean flags (C, V, Z, N, X) for fast branching
- **All 16 condition codes**: EQ, NE, HI, LS, CC, CS, VC, VS, PL, MI, GE, LT, GT, LE
- **Arithmetic macros**: ADD, SUB, CMP, NEG, MULU, MULS, DIVU, DIVS (8/16/32-bit)
- **Logical macros**: AND, OR, EOR, NOT, BTST, BSET, BCLR, BCHG, TST
- **Shift/rotate macros**: LSL, LSR, ASR, ROL, ROR, ROXL, ROXR
- **Misc**: SWAP, EXT, ADDX, SUBX, NEGX
- CCR/SR get/set helpers

### Memory Bus (`include/genrecomp/bus.h`)

Flat 24-bit address space with big-endian byte ordering, routed through Genesis Plus GX's
memory map for full hardware accuracy:

```c
bus_write16(0xC00004, value);         // Write to VDP control port → real VDP
bus_write8(0xA04000, ym_addr);        // Write to YM2612 → real FM synth
uint16_t joy = bus_read16(0xA10003);  // Read controller → real I/O system
bus_ram_write32(0x1000, data);        // Fast direct RAM write (bypass bus)
```

### Function Dispatch (`include/genrecomp/func_table.h`)

Hash table mapping M68K addresses to native function pointers:

```c
func_table_register(0x000200, my_func);  // Register
func_table_call(0x000200);               // Call by address (for JSR/BSR)
gen_func_t fn = func_table_lookup(0x000200); // Lookup (for indirect calls)
```

### Hardware Adapters

| Adapter | Header | Backend |
|---------|--------|---------|
| VDP (graphics) | `vdp.h` | Genesis Plus GX `vdp_ctrl.c` + `vdp_render.c` |
| YM2612 (FM audio) | `ym2612.h` | Genesis Plus GX `sound/ym2612.c` |
| SN76489 (PSG audio) | `psg.h` | Genesis Plus GX `sound/psg.c` |
| Z80 (coprocessor) | `z80.h` | Genesis Plus GX `z80/z80.c` |
| I/O (controllers) | `io.h` | Genesis Plus GX `io_ctrl.c` + `input_hw/` |

### Debug Framework (`include/genrecomp/debug.h`)

Compile-time `GENRECOMP_DEBUG` flag gates all instrumentation (zero overhead in release):

- **Execution tracing** — Log every function entry with address + name
- **Breakpoints** — Break at specific M68K addresses
- **Memory watchpoints** — Break on read/write to specific addresses
- **GDB remote stub** — TCP-based RSP server, attach with standard GDB (planned)
- **CPU state dump** — All registers, flags, cycle counts

## Genesis Hardware Reference

The Genesis memory map (from the M68K's perspective):

| Address Range | Size | Description |
|---------------|------|-------------|
| `$000000-$3FFFFF` | 4MB | Cartridge ROM |
| `$A00000-$A0FFFF` | 64KB | Z80 address space (RAM + sound chips) |
| `$A10000-$A1001F` | 32B | I/O ports (controllers, version) |
| `$A11100-$A11101` | 2B | Z80 bus request |
| `$A11200-$A11201` | 2B | Z80 reset |
| `$C00000-$C00003` | 4B | VDP data port |
| `$C00004-$C00007` | 4B | VDP control port |
| `$C00008-$C0000F` | 8B | VDP HV counter |
| `$C00011` | 1B | PSG output |
| `$FF0000-$FFFFFF` | 64KB | M68K work RAM |

## Standing on the Shoulders of Giants

### Hardware Backend

- **[Genesis Plus GX](https://github.com/ekeeke/Genesis-Plus-GX)** (Non-commercial)
  — The most modular and accurate Genesis emulator core. Provides our VDP, YM2612, PSG,
  Z80, and memory bus implementations. Already proven as a libretro core across dozens
  of platforms.

- **[ymfm](https://github.com/aaronsgiles/ymfm)** (BSD-3)
  — Standalone YM2612 FM synthesis by the MAME lead developer. Used as an alternative
  high-quality sound backend.

### Architectural Inspiration

- **[N64Recomp](https://github.com/N64Recomp/N64Recomp)** (MIT)
  — Pioneered the static recompilation approach for N64 games.

- **[snesrecomp](https://github.com/sp00nznet/snesrecomp)** (MIT)
  — Our SNES sibling project. genrecomp follows the exact same architectural pattern.

### Read-Only Reference

- **[BlastEm](https://www.retrodev.com/blastem/)** (GPLv3)
  — The most accurate Genesis emulator. Referenced for GDB stub protocol design and
  hardware behavior verification. No code copied.

## Licensing

genrecomp is released under the **MIT License**. Use it for anything — commercial projects,
homebrew, research, education. Just keep the attribution.

**No code from GPL-licensed projects (BlastEm) has been incorporated.**
BlastEm is referenced only for behavioral correctness verification.

## Legal

This project does not include any copyrighted game assets, code, or ROMs. You must provide
your own legally obtained Genesis/Mega Drive game ROM. genrecomp is a tool — what you do
with it is your responsibility.

## Contributing

Found a bug? Want to add support for a hardware feature? Have a game you're trying to
bring up? PRs and issues are welcome!

The Genesis library is vast and legendary. There are hundreds of games waiting to be
recompiled. Every contribution — whether it's a missing VDP mode, a sound timing fix,
or documentation for a hardware quirk — helps the whole community.

## Part of sp00nznet

Built with love for the games that defined a generation.

---

*"Genesis does what Nintendon't — and now it does it natively."*
