/*
 * Controller state into Genesis Plus GX's input system. GPGX's input.pad[]
 * uses the same bit layout as GEN_BTN_* (input.h), so no translation.
 */

#include "genrecomp/io.h"
#include "shared.h"

void io_set_pad_state(int port, uint16_t buttons) {
    if (port >= 0 && port < MAX_DEVICES) {
        input.pad[port] = buttons;
    }
}
