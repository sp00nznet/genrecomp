/*
 * genrecomp — Genesis/Mega Drive Hardware Backend Library
 * https://github.com/sp00nznet/genrecomp
 *
 * Drop-in Genesis hardware for your static recompilation project.
 * Powered by Genesis Plus GX — real VDP rendering, YM2612 + PSG audio,
 * Z80 coprocessor, the whole deal. You bring the recompiled game code,
 * we bring the hardware.
 *
 * Usage:
 *   #include <genrecomp/genrecomp.h>
 *
 *   genrecomp_init("My Genesis Recomp", 3);
 *   genrecomp_load_rom("game.md");
 *   while (running) {
 *       genrecomp_begin_frame();
 *       // ... run recompiled functions ...
 *       bus_write16(0xC00004, value);  // writes hit real VDP
 *       genrecomp_end_frame();         // renders + presents
 *   }
 *   genrecomp_shutdown();
 */

#ifndef GENRECOMP_H
#define GENRECOMP_H

#include "m68k.h"
#include "bus.h"
#include "func_table.h"
#include "io.h"
#include "platform.h"
#include "input.h"

#include <stdbool.h>

/*
 * Initialize all Genesis hardware subsystems and the SDL2 platform layer.
 * Call this once at startup before anything else.
 *
 * window_title: displayed in the SDL2 window title bar
 * scale:        integer scale factor (e.g. 3 = 960x672 window)
 */
bool genrecomp_init(const char *window_title, int scale);

/*
 * Load a ROM file (.md / .bin / .gen). Handles interleaved format
 * detection and region detection automatically.
 * Returns true on success.
 */
bool genrecomp_load_rom(const char *path);

/*
 * Call at the start of each frame before running recompiled game code.
 * Polls input, updates controller state, and prepares the frame.
 * Returns false if the user requested quit (window close / Escape).
 */
bool genrecomp_begin_frame(void);

/*
 * Call at the end of each frame after running recompiled game code.
 * Renders all VDP scanlines, mixes audio, presents the framebuffer
 * via SDL2, and syncs to ~60 Hz NTSC timing.
 */
void genrecomp_end_frame(void);

/*
 * Trigger VBlank processing — VBlank flag, interrupt handling.
 * Call this when your recompiled code reaches the VBlank point.
 */
void genrecomp_trigger_vblank(void);

/*
 * Get a pointer to the rendered framebuffer (320x224 RGBX8888).
 * Valid after genrecomp_end_frame().
 */
const uint8_t *genrecomp_get_framebuffer(void);

/*
 * Clean shutdown — frees all Genesis hardware and closes SDL2.
 */
void genrecomp_shutdown(void);

#endif /* GENRECOMP_H */
