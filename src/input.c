/*
 * Genesis controller input — keyboard/gamepad mapping.
 */

#include "genrecomp/input.h"
#include "genrecomp/io.h"

#include <SDL.h>
#include <string.h>

static uint16_t s_pad_state[2];

void recomp_input_update(void) {
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint16_t buttons = 0;

    /* Default keyboard mapping (Player 1) */
    if (keys[SDL_SCANCODE_UP])     buttons |= GEN_BTN_UP;
    if (keys[SDL_SCANCODE_DOWN])   buttons |= GEN_BTN_DOWN;
    if (keys[SDL_SCANCODE_LEFT])   buttons |= GEN_BTN_LEFT;
    if (keys[SDL_SCANCODE_RIGHT])  buttons |= GEN_BTN_RIGHT;
    if (keys[SDL_SCANCODE_Z])      buttons |= GEN_BTN_A;
    if (keys[SDL_SCANCODE_X])      buttons |= GEN_BTN_B;
    if (keys[SDL_SCANCODE_C])      buttons |= GEN_BTN_C;
    if (keys[SDL_SCANCODE_RETURN]) buttons |= GEN_BTN_START;
    if (keys[SDL_SCANCODE_A])      buttons |= GEN_BTN_X;
    if (keys[SDL_SCANCODE_S])      buttons |= GEN_BTN_Y;
    if (keys[SDL_SCANCODE_D])      buttons |= GEN_BTN_Z;

    s_pad_state[0] = buttons;
    s_pad_state[1] = 0;

    /* Feed into I/O system */
    io_set_pad_state(0, s_pad_state[0]);
    io_set_pad_state(1, s_pad_state[1]);
}

uint16_t recomp_input_read_pad(int port) {
    if (port < 0 || port > 1) return 0;
    return s_pad_state[port];
}
