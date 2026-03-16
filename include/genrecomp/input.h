#ifndef GENRECOMP_INPUT_H
#define GENRECOMP_INPUT_H

/*
 * Genesis controller input — maps keyboard/gamepad to Genesis pad state.
 *
 * Button state is automatically fed into the I/O system during
 * genrecomp_begin_frame(), so controller reads from recompiled code
 * work exactly like real hardware.
 */

#include <stdint.h>

/* Genesis 3-button pad button indices */
#define GEN_BTN_UP      0x0001
#define GEN_BTN_DOWN    0x0002
#define GEN_BTN_LEFT    0x0004
#define GEN_BTN_RIGHT   0x0008
#define GEN_BTN_B       0x0010
#define GEN_BTN_C       0x0020
#define GEN_BTN_A       0x0040
#define GEN_BTN_START   0x0080

/* Genesis 6-button pad extra buttons */
#define GEN_BTN_X       0x0100
#define GEN_BTN_Y       0x0200
#define GEN_BTN_Z       0x0400
#define GEN_BTN_MODE    0x0800

/* Update keyboard/gamepad state and feed into I/O system */
void recomp_input_update(void);

/* Read current button state for a port (0-1) */
uint16_t recomp_input_read_pad(int port);

#endif /* GENRECOMP_INPUT_H */
