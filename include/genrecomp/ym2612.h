#ifndef GENRECOMP_YM2612_H
#define GENRECOMP_YM2612_H

/*
 * YM2612 FM synthesis API.
 * All functions prefixed with genrecomp_ym2612_ to avoid collision
 * with GenPlusGX's internal ym2612 functions.
 */

#include <stdint.h>
#include <stdbool.h>

bool genrecomp_ym2612_init(int sample_rate);
void genrecomp_ym2612_shutdown(void);
void genrecomp_ym2612_write(uint8_t port, uint8_t addr, uint8_t data);
void genrecomp_ym2612_update(int16_t *buffer, int sample_count);
void genrecomp_ym2612_reset(void);

#endif /* GENRECOMP_YM2612_H */
