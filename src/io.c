/*
 * Controller I/O port handling — wraps Genesis Plus GX's I/O system.
 *
 * Genesis I/O region ($A10001-$A1001F) is handled by GenPlusGX's
 * io_ctrl module through the memory map. This adapter provides a
 * clean API and bridges our input system to GenPlusGX's input system.
 */

#include "genrecomp/io.h"

/* GenPlusGX headers */
#include "shared.h"

bool genrecomp_io_init(void) {
    /* I/O is initialized by system_init() -> io_init() in GenPlusGX */
    return true;
}

void genrecomp_io_shutdown(void) {
    /* Nothing — GenPlusGX handles cleanup */
}

uint8_t io_read(uint32_t addr) {
    /* Route through GenPlusGX's I/O read handler */
    return (uint8_t)io_68k_read((addr >> 1) & 0x0F);
}

void io_write(uint32_t addr, uint8_t val) {
    /* Route through GenPlusGX's I/O write handler */
    io_68k_write((addr >> 1) & 0x0F, val);
}

void io_set_pad_state(int port, uint16_t buttons) {
    /* Feed button state into GenPlusGX's input system.
     * GenPlusGX's input.pad[] uses the same bitmask layout
     * as our GEN_BTN_* defines (by design). */
    if (port >= 0 && port < MAX_DEVICES) {
        input.pad[port] = buttons;
    }
}
