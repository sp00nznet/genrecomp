/*
 * genrecomp core lifecycle — init, frame, shutdown.
 *
 * This is the glue that ties Genesis Plus GX hardware, the recomp
 * CPU state, and the SDL2 platform layer together.
 */

#include "genrecomp/genrecomp.h"

/* GenPlusGX headers */
#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Defined in osd.c */
extern void genrecomp_config_default(void);

/* Pixel buffer for VDP output (720x576x4 — GenPlusGX's max render size) */
#define GPGX_BITMAP_WIDTH  720
#define GPGX_BITMAP_HEIGHT 576
static uint32_t s_bitmap_data[GPGX_BITMAP_WIDTH * GPGX_BITMAP_HEIGHT];

#define AUDIO_SAMPLE_RATE  44100

static bool s_initialized = false;

bool genrecomp_init(const char *window_title, int scale) {
    if (s_initialized) return true;

    /* Set up GenPlusGX config defaults */
    genrecomp_config_default();

    /* Initialize bitmap structure — GenPlusGX renders into this */
    memset(&bitmap, 0, sizeof(bitmap));
    bitmap.width  = GPGX_BITMAP_WIDTH;
    bitmap.height = GPGX_BITMAP_HEIGHT;
    bitmap.pitch  = GPGX_BITMAP_WIDTH * 4;  /* 32bpp RGBX */
    bitmap.data   = (uint8 *)s_bitmap_data;

    /* Initialize function dispatch table */
    func_table_init();

    /* Initialize SDL2 platform */
    if (!platform_init(window_title, scale)) {
        fprintf(stderr, "genrecomp: failed to initialize platform\n");
        return false;
    }

    s_initialized = true;
    printf("genrecomp: initialized (Genesis Plus GX backend)\n");
    return true;
}

bool genrecomp_load_rom(const char *path) {
    if (!s_initialized) return false;

    /* GenPlusGX's load_rom expects a mutable char* */
    char pathbuf[MAXPATHLEN];
    strncpy(pathbuf, path, sizeof(pathbuf) - 1);
    pathbuf[sizeof(pathbuf) - 1] = '\0';

    /* Load ROM via GenPlusGX (calls our load_archive in osd.c) */
    if (!load_rom(pathbuf)) {
        fprintf(stderr, "genrecomp: GenPlusGX rejected the ROM: %s\n", path);
        return false;
    }

    /* Initialize audio subsystem */
    audio_init(AUDIO_SAMPLE_RATE, 0);

    /* Two gamepads. Must precede system_init(): its io_init() binds the
     * port handlers from input.system, and without it both ports stay on
     * dummy handlers that never report a button. */
    input.system[0] = SYSTEM_GAMEPAD;
    input.system[1] = SYSTEM_GAMEPAD;

    /* Initialize all hardware subsystems */
    system_init();

    /* Hard reset everything */
    system_reset();

    /* Re-point bitmap after reset */
    bitmap.data = (uint8 *)s_bitmap_data;

    /* Initialize recomp CPU state from the vector table
     * (now that ROM is loaded and memory map is set up) */
    recomp_m68k_reset();


    printf("genrecomp: ROM loaded — \"%s\"\n", rominfo.international);
    printf("genrecomp: System: %s | Region: %s\n",
           rominfo.consoletype, rominfo.country);
    printf("genrecomp: PC=$%06X SP=$%08X\n",
           g_m68k.pc, g_m68k.ssp);

    return true;
}

bool genrecomp_begin_frame(void) {
    /* Poll SDL events */
    if (!platform_poll_events()) {
        return false;
    }

    /* Update input and feed into GenPlusGX's input system */
    recomp_input_update();


    return true;
}

void genrecomp_end_frame(void) {
    /*
     * Run a full frame of VDP rendering.
     *
     * In normal GenPlusGX, system_frame_gen() interleaves M68K/Z80
     * execution with per-scanline VDP rendering. Since our M68K is
     * replaced by recompiled code, we just render all scanlines after
     * the recompiled frame code has finished updating VDP state.
     */
    int active_lines = bitmap.viewport.h;

    /* GPGX defers an H32/H40 switch made mid-display to the next frame
     * boundary inside system_frame_gen(), which never runs here. We draw
     * the whole frame at once, so take the width the registers say now. */
    if (reg[1] & 0x04)
        bitmap.viewport.w = (reg[12] & 0x01) ? 320 : 256;

    /* Guard: skip rendering if VDP isn't initialized yet */
    if (active_lines <= 0 || active_lines > 240 || !bitmap.data) return;

    /* Save and restore v_counter so rendering doesn't disturb
     * the cycle simulation's scanline tracking */
    uint16_t saved_v = v_counter;

    /* Render active display lines.
     * Guard against uninitialized VDP state:
     * - Mode 5 must be set (reg[1] bit 2)
     * - Display must be enabled (reg[1] bit 6)
     * - Viewport width must be valid */
    int vp_w = bitmap.viewport.w;
    if ((reg[1] & 0x44) && vp_w > 0 && vp_w <= 320) {
        for (int line = 0; line < active_lines; line++) {
            v_counter = line;
            render_line(line);
        }
    }

    /* Restore v_counter */
    v_counter = saved_v;

    /* Signal VBlank */
    status |= 0x08; /* VBlank flag */

    /* Audio is mixed at the end of each simulated frame in bus.c */

    /* Present: copy the viewport into a compact 320x224 buffer */
    int vp_h = bitmap.viewport.h;
    if (vp_w <= 0 || vp_h <= 0) {
        platform_frame_sync();
        return;
    }

    uint8_t *fb_start = bitmap.data + (bitmap.viewport.y * bitmap.pitch) + (bitmap.viewport.x * 4);
    static uint8_t s_present_buf[GEN_RENDER_WIDTH * GEN_RENDER_HEIGHT * 4];

    int copy_w = (vp_w > GEN_RENDER_WIDTH) ? GEN_RENDER_WIDTH : vp_w;
    int copy_h = (vp_h > GEN_RENDER_HEIGHT) ? GEN_RENDER_HEIGHT : vp_h;

    /* H32 is narrower than the buffer: don't leave the last H40 frame's
     * right-hand columns on screen */
    if (copy_w < GEN_RENDER_WIDTH) memset(s_present_buf, 0, sizeof(s_present_buf));

    for (int y = 0; y < copy_h; y++) {
        memcpy(s_present_buf + y * GEN_RENDER_WIDTH * 4,
               fb_start + y * bitmap.pitch,
               copy_w * 4);
    }

    platform_present_frame(s_present_buf);

    /* Frame sync */
    platform_frame_sync();
}

void genrecomp_trigger_vblank(void) {
    /* Set VDP VBlank flag */
    status |= 0x08;
    vint_pending = 1;

    /* Trigger level 6 interrupt on the recomp CPU */
    /* (recompiled code checks this and calls the VBlank handler) */
}

const uint8_t *genrecomp_get_framebuffer(void) {
    return bitmap.data;
}

void genrecomp_shutdown(void) {
    if (!s_initialized) return;

    platform_shutdown();
    audio_shutdown();

    s_initialized = false;
    printf("genrecomp: shutdown complete\n");
}
