/*
 * osd.c — genrecomp OSD implementations for Genesis Plus GX.
 *
 * Provides the platform-dependent functions that GenPlusGX's core
 * expects to exist: load_archive, osd_input_update, and globals.
 */

#include "osd.h"

/* GenPlusGX shared header (pulls in all core headers) */
#include "shared.h"

/* genrecomp input header (for feeding input into GenPlusGX) */
#include "genrecomp/input.h"

/* Global configuration */
t_config config;

/* BIOS/ROM path globals (unused but must exist for linker) */
char GG_ROM[256];
char AR_ROM[256];
char SK_ROM[256];
char SK_UPMEM[256];
char GG_BIOS[256];
char MD_BIOS[256];
char CD_BIOS_EU[256];
char CD_BIOS_US[256];
char CD_BIOS_JP[256];
char MS_BIOS_US[256];
char MS_BIOS_EU[256];
char MS_BIOS_JP[256];

/*
 * Initialize config to sensible defaults for Mega Drive.
 */
void genrecomp_config_default(void)
{
    memset(&config, 0, sizeof(config));

    /* Sound */
    config.hq_fm     = 1;
    config.hq_psg    = 0;
    config.filter    = 0;
    config.ym2612    = 0; /* 0 = MAME YM2612 core */
    config.ym2413    = 0;
    config.mono      = 0;
    config.psg_preamp  = 150;
    config.fm_preamp   = 100;
    config.cdda_volume = 100;
    config.pcm_volume  = 100;
    config.lp_range  = 0;
    config.low_freq  = 0;
    config.high_freq = 0;
    config.lg = 1;
    config.mg = 1;
    config.hg = 1;

    /* System */
    config.system        = 0;       /* 0 = auto-detect */
    config.region_detect = 0;       /* 0 = auto-detect */
    config.master_clock  = 0;       /* 0 = auto-detect */
    config.vdp_mode      = 0;       /* 0 = auto-detect */
    config.force_dtack   = 0;
    config.addr_error    = 1;
    config.bios          = 0;       /* No BIOS */
    config.lock_on       = 0;
    config.add_on        = 0;
    config.overscan      = 0;
    config.aspect_ratio  = 0;
    config.ntsc          = 0;
    config.lcd           = 0;
    config.gg_extra      = 0;
    config.left_border   = 0;
    config.render        = 0;

    /* Input — default 2 gamepads */
    config.input[0].device  = 1;    /* SYSTEM_GAMEPAD */
    config.input[0].port    = 0;
    config.input[0].padtype = 0;    /* DEVICE_PAD3B */
    config.input[1].device  = 1;
    config.input[1].port    = 1;
    config.input[1].padtype = 0;

    /* No sprite limit */
    config.no_sprite_limit = 0;
    config.enhanced_vscroll = 0;
    config.enhanced_vscroll_limit = 0;
    config.overclock = 0;
    config.cd_latency = 0;
}

/*
 * load_archive — called by GenPlusGX's load_rom() to load ROM data.
 * We handle this ourselves by reading the file directly.
 */
int load_archive(char *filename, unsigned char *buffer, int maxsize, char *extension)
{
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > maxsize) size = maxsize;

    size_t nread = fread(buffer, 1, (size_t)size, f);
    fclose(f);

    /* Set extension if requested */
    if (extension) {
        const char *dot = strrchr(filename, '.');
        if (dot) {
            strncpy(extension, dot, 15);
            extension[15] = '\0';
        } else {
            strcpy(extension, ".bin");
        }
    }

    return (int)nread;
}

/*
 * osd_input_update — called by GenPlusGX during system_frame_gen()
 * to poll input. We feed our genrecomp input state into GenPlusGX's
 * input system.
 */
void osd_input_update(void)
{
    /* Read genrecomp pad state and translate to GenPlusGX INPUT_* bitmasks */
    uint16_t pad0 = recomp_input_read_pad(0);
    uint16_t pad1 = recomp_input_read_pad(1);

    /* GenPlusGX input bitmasks (from input_hw/input.h):
     * INPUT_UP=0x0001 INPUT_DOWN=0x0002 INPUT_LEFT=0x0004 INPUT_RIGHT=0x0008
     * INPUT_B=0x0010  INPUT_C=0x0020    INPUT_A=0x0040    INPUT_START=0x0080
     * INPUT_Z=0x0100  INPUT_Y=0x0200    INPUT_X=0x0400    INPUT_MODE=0x0800
     *
     * Our GEN_BTN_ values happen to match exactly (by design).
     */
    input.pad[0] = pad0;
    input.pad[1] = pad1;
}
