/*
 * osd.h — genrecomp OSD (Operating System Dependent) layer for Genesis Plus GX.
 *
 * This replaces the libretro osd.h. It provides all the definitions,
 * types, and function declarations that GenPlusGX's shared.h expects
 * from the platform layer.
 */

#ifndef _OSD_H
#define _OSD_H

#ifdef _MSC_VER
#include <stdio.h>
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
/* MSVC doesn't support __inline__, define INLINE before macros.h picks its default */
#ifndef INLINE
#define INLINE static __inline
#endif
#else
/* GCC/Clang */
#ifndef INLINE
#define INLINE static inline
#endif
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Basic constants
 * ================================================================ */

#define MAX_INPUTS  8
#define MAX_KEYS    8
#define MAXPATHLEN  1024

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef M_PI
#define M_PI 3.1415926535897932385
#endif

/* ================================================================
 * Pixel format — we use 32bpp RGBX for direct SDL texture upload
 * ================================================================ */
#ifndef USE_32BPP_RENDERING
#define USE_32BPP_RENDERING
#endif
#ifndef LSB_FIRST
#define LSB_FIRST  /* x86 is little-endian */
#endif

/* ================================================================
 * Sprite limits (allow no-sprite-limit mode)
 * ================================================================ */
#ifndef HAVE_NO_SPRITE_LIMIT
#define HAVE_NO_SPRITE_LIMIT
#endif
#define MAX_SPRITES_PER_LINE 80
#define TMS_MAX_SPRITES_PER_LINE     (config.no_sprite_limit ? MAX_SPRITES_PER_LINE : 4)
#define MODE4_MAX_SPRITES_PER_LINE   (config.no_sprite_limit ? MAX_SPRITES_PER_LINE : 8)
#define MODE5_MAX_SPRITES_PER_LINE   (config.no_sprite_limit ? MAX_SPRITES_PER_LINE : (bitmap.viewport.w >> 4))
#define MODE5_MAX_SPRITE_PIXELS      (config.no_sprite_limit ? MAX_SPRITES_PER_LINE * 32 : max_sprite_pixels)

/* ================================================================
 * Cheat system (stub)
 * ================================================================ */
#define CHEATS_UPDATE() ((void)0)

/* ================================================================
 * CD stream operations (stub — no Mega CD support in Phase 2)
 * ================================================================ */
#ifndef cdStream
#define cdStream            FILE
#define cdStreamOpen(fname) fopen(fname, "rb")
#define cdStreamClose       fclose
#define cdStreamRead        fread
#define cdStreamSeek        fseek
#define cdStreamTell        ftell
#define cdStreamGets        fgets
#endif

/* ================================================================
 * Input configuration
 * ================================================================ */
typedef struct
{
    int8_t device;
    uint8_t port;
    uint8_t padtype;
} t_input_config;

/* ================================================================
 * Global configuration
 * ================================================================ */
typedef struct
{
    char version[16];
    uint8_t hq_fm;
    uint8_t filter;
    uint8_t hq_psg;
    uint8_t ym2612;
    uint8_t ym2413;
    uint8_t mono;
    int16_t psg_preamp;
    int16_t fm_preamp;
    int16_t cdda_volume;
    int16_t pcm_volume;
    uint16_t lp_range;
    int16_t low_freq;
    int16_t high_freq;
    int16_t lg;
    int16_t mg;
    int16_t hg;
    uint8_t system;
    uint8_t region_detect;
    uint8_t master_clock;
    uint8_t vdp_mode;
    uint8_t force_dtack;
    uint8_t addr_error;
    uint8_t bios;
    uint8_t lock_on;
    uint8_t add_on;
    uint8_t overscan;
    uint8_t aspect_ratio;
    uint8_t ntsc;
    uint8_t lcd;
    uint8_t gg_extra;
    uint8_t left_border;
    uint8_t render;
    t_input_config input[MAX_INPUTS];
    uint8_t invert_mouse;
    uint8_t gun_cursor;
    uint32_t overclock;
    uint8_t no_sprite_limit;
    uint8_t enhanced_vscroll;
    uint8_t enhanced_vscroll_limit;
    uint8_t cd_latency;
} t_config;

extern t_config config;

/* ================================================================
 * BIOS/ROM path strings (unused but required by GenPlusGX)
 * ================================================================ */
extern char GG_ROM[256];
extern char AR_ROM[256];
extern char SK_ROM[256];
extern char SK_UPMEM[256];
extern char GG_BIOS[256];
extern char MD_BIOS[256];
extern char CD_BIOS_EU[256];
extern char CD_BIOS_US[256];
extern char CD_BIOS_JP[256];
extern char MS_BIOS_US[256];
extern char MS_BIOS_EU[256];
extern char MS_BIOS_JP[256];

/* ================================================================
 * OSD function prototypes (called by GenPlusGX core)
 * ================================================================ */

/* Called during system_frame_gen() to poll input */
extern void osd_input_update(void);

/* Called by load_rom() to load ROM data from file */
extern int load_archive(char *filename, unsigned char *buffer, int maxsize, char *extension);

/* CRC32 — stub since we don't need cheat support */
static inline unsigned int crc32(unsigned int crc, const unsigned char *buf, unsigned int len) {
    (void)crc; (void)buf; (void)len;
    return 0;
}

#endif /* _OSD_H */
