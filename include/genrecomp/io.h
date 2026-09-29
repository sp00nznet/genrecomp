#ifndef GENRECOMP_IO_H
#define GENRECOMP_IO_H

/*
 * Controller state into Genesis Plus GX's I/O chip. Reads and writes of
 * $A10001-$A1001F from recompiled code go through the bus to GPGX's
 * io_ctrl directly; this is only the input layer's way in.
 */

#include <stdint.h>

/* Button bitmask: Up=0x01 Down=0x02 Left=0x04 Right=0x08 B=0x10 C=0x20 A=0x40 Start=0x80 */
/* Extra buttons (6-button): X=0x100 Y=0x200 Z=0x400 Mode=0x800 */
void io_set_pad_state(int port, uint16_t buttons);

#endif /* GENRECOMP_IO_H */
