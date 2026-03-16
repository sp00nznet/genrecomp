/*
 * Z80 coprocessor bridge — wraps Genesis Plus GX's Z80 core.
 */

#include "genrecomp/z80.h"
#include "shared.h"

bool genrecomp_z80_init(void) {
    return true;
}

void genrecomp_z80_shutdown(void) {
}

void genrecomp_z80_run(int cycles) {
    z80_execute(cycles);
}

void genrecomp_z80_bus_request(bool request) {
    gen_zbusreq_w(request ? 1 : 0, m68k.cycles);
}

bool genrecomp_z80_bus_granted(void) {
    return (zstate & 2) != 0;
}

void genrecomp_z80_reset(void) {
    gen_zreset_w(0, m68k.cycles);
    gen_zreset_w(1, m68k.cycles);
}

uint8_t genrecomp_z80_read(uint16_t addr) {
    return (uint8_t)z80_read_byte(0xA00000 | addr);
}

void genrecomp_z80_write(uint16_t addr, uint8_t val) {
    z80_write_byte(0xA00000 | addr, val);
}
