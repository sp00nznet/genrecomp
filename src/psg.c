/*
 * SN76489 PSG adapter — wraps Genesis Plus GX's PSG core.
 */

#include "genrecomp/psg.h"
#include "shared.h"

bool genrecomp_psg_init(int sample_rate) {
    (void)sample_rate;
    return true;
}

void genrecomp_psg_shutdown(void) {
}

void genrecomp_psg_write(uint8_t val) {
    SN76489_Write(m68k.cycles, val);
}

void genrecomp_psg_update(int16_t *buffer, int sample_count) {
    if (buffer) {
        memset(buffer, 0, (size_t)sample_count * 2 * sizeof(int16_t));
    }
}

void genrecomp_psg_reset(void) {
    SN76489_Reset();
}
