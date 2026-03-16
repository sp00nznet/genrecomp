#ifndef GENRECOMP_Z80_H
#define GENRECOMP_Z80_H

/*
 * Z80 coprocessor bridge.
 * All functions prefixed with genrecomp_z80_ to avoid collision
 * with GenPlusGX's internal z80 functions.
 */

#include <stdint.h>
#include <stdbool.h>

bool genrecomp_z80_init(void);
void genrecomp_z80_shutdown(void);
void genrecomp_z80_run(int cycles);
void genrecomp_z80_bus_request(bool request);
bool genrecomp_z80_bus_granted(void);
void genrecomp_z80_reset(void);
uint8_t genrecomp_z80_read(uint16_t addr);
void    genrecomp_z80_write(uint16_t addr, uint8_t val);

#endif /* GENRECOMP_Z80_H */
