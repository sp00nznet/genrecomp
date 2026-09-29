/*
 * genrecomp reference runner: plays a ROM on Genesis Plus GX's own 68K
 * interpreter, with the same flags as a recomp (--headless, --record,
 * --frames, --press, --ram-dump). It is the ground truth a recomp is
 * compared against: record both, or dump RAM from both and diff, and the
 * first frame they disagree on is where the recompiled code went wrong.
 *
 *   genrecomp_ref --headless --frames 1800 --ram-dump ref:60 rom.gen
 */

#include <genrecomp/genrecomp.h>
#include <genrecomp/platform.h>
#include <genrecomp/input.h>
#include <stdio.h>
#include <string.h>

#include "shared.h"

int main(int argc, char *argv[]) {
    argc = platform_parse_args(argc, argv);
    if (argc < 2) {
        fprintf(stderr, "usage: genrecomp_ref [--headless] [--record out.mp4] "
                        "[--frames N] [--press F:BTN[:LEN]] [--ram-dump DIR:EVERY] rom.gen\n");
        return 1;
    }
    if (!genrecomp_init("genrecomp reference", 3)) return 1;

    /* Not genrecomp_load_rom: that stubs Z80 bus control for recompiled
     * code, and the reference must run the untouched hardware. */
    char path[MAXPATHLEN];
    strncpy(path, argv[1], sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    if (!load_rom(path)) { fprintf(stderr, "ref: cannot load %s\n", argv[1]); return 1; }
    audio_init(44100, 0);
    input.system[0] = SYSTEM_GAMEPAD;  /* before system_init: io_init binds ports */
    input.system[1] = SYSTEM_GAMEPAD;
    system_init();
    system_reset();

    static uint8_t present[GEN_RENDER_WIDTH * GEN_RENDER_HEIGHT * 4];
    static int16_t audio[4096];
    for (;;) {
        if (!platform_poll_events()) break;
        recomp_input_update();
        system_frame_gen(0);
        audio_update(audio);

        memset(present, 0, sizeof(present));
        int w = bitmap.viewport.w < GEN_RENDER_WIDTH ? bitmap.viewport.w : GEN_RENDER_WIDTH;
        int h = bitmap.viewport.h < GEN_RENDER_HEIGHT ? bitmap.viewport.h : GEN_RENDER_HEIGHT;
        const uint8_t *src = bitmap.data + bitmap.viewport.y * bitmap.pitch + bitmap.viewport.x * 4;
        for (int y = 0; y < h; y++)
            memcpy(present + y * GEN_RENDER_WIDTH * 4, src + y * bitmap.pitch, (size_t)w * 4);
        platform_present_frame(present);
        platform_frame_sync();
    }
    genrecomp_shutdown();
    return 0;
}
