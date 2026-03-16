/*
 * YM2612 FM synthesis adapter — wraps Genesis Plus GX's YM2612 core.
 */

#include "genrecomp/ym2612.h"
#include "shared.h"

bool genrecomp_ym2612_init(int sample_rate) {
    (void)sample_rate;
    return true;
}

void genrecomp_ym2612_shutdown(void) {
}

void genrecomp_ym2612_write(uint8_t port, uint8_t addr, uint8_t data) {
    if (fm_write) {
        unsigned int base = port ? 2 : 0;
        fm_write(m68k.cycles, base, addr);
        fm_write(m68k.cycles, base + 1, data);
    }
}

void genrecomp_ym2612_update(int16_t *buffer, int sample_count) {
    if (buffer && sample_count > 0) {
        int generated = audio_update(buffer);
        if (generated < sample_count) {
            memset(buffer + generated * 2, 0,
                   (size_t)(sample_count - generated) * 2 * sizeof(int16_t));
        }
    }
}

void genrecomp_ym2612_reset(void) {
    if (fm_reset) fm_reset(0);
}
