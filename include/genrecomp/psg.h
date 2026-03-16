#ifndef GENRECOMP_PSG_H
#define GENRECOMP_PSG_H

/*
 * SN76489 PSG (Programmable Sound Generator) API.
 * All functions prefixed with genrecomp_psg_ to avoid collision
 * with GenPlusGX's internal psg functions.
 */

#include <stdint.h>
#include <stdbool.h>

bool genrecomp_psg_init(int sample_rate);
void genrecomp_psg_shutdown(void);
void genrecomp_psg_write(uint8_t val);
void genrecomp_psg_update(int16_t *buffer, int sample_count);
void genrecomp_psg_reset(void);

#endif /* GENRECOMP_PSG_H */
