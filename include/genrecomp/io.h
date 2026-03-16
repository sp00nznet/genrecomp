#ifndef GENRECOMP_IO_H
#define GENRECOMP_IO_H

/*
 * Controller I/O ports.
 *
 * Genesis I/O region ($A10001-$A1001F) handles controller reads,
 * region/version detection, and serial communication.
 * Wraps Genesis Plus GX's I/O system.
 */

#include <stdint.h>
#include <stdbool.h>

bool genrecomp_io_init(void);
void genrecomp_io_shutdown(void);

/* Read/write I/O registers ($A10001-$A1001F) */
uint8_t io_read(uint32_t addr);
void    io_write(uint32_t addr, uint8_t val);

/* Set controller button state (for input layer to call) */
/* Button bitmask: Up=0x01 Down=0x02 Left=0x04 Right=0x08 B=0x10 C=0x20 A=0x40 Start=0x80 */
/* Extra buttons (6-button): X=0x100 Y=0x200 Z=0x400 Mode=0x800 */
void io_set_pad_state(int port, uint16_t buttons);

#endif /* GENRECOMP_IO_H */
