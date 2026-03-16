/*
 * VDP adapter — wraps Genesis Plus GX VDP.
 */

#include "genrecomp/vdp.h"

/* GenPlusGX headers */
#include "shared.h"

bool genrecomp_vdp_init(void) {
    return true;
}

void genrecomp_vdp_shutdown(void) {
}

void genrecomp_vdp_write_reg(uint8_t r, uint8_t val) {
    uint16_t cmd = (uint16_t)(0x8000 | ((uint16_t)r << 8) | val);
    vdp_68k_ctrl_w(cmd);
}

uint8_t genrecomp_vdp_read_reg(uint8_t r) {
    if (r < 0x20) return reg[r];
    return 0;
}

void genrecomp_vdp_write_data(uint16_t val) {
    if (vdp_68k_data_w) vdp_68k_data_w(val);
}

uint16_t genrecomp_vdp_read_data(void) {
    if (vdp_68k_data_r) return (uint16_t)vdp_68k_data_r();
    return 0;
}

void genrecomp_vdp_write_ctrl(uint16_t val) {
    vdp_68k_ctrl_w(val);
}

uint16_t genrecomp_vdp_read_status(void) {
    return (uint16_t)vdp_68k_ctrl_r(m68k.cycles);
}

uint16_t genrecomp_vdp_read_hv(void) {
    return (uint16_t)vdp_hvc_r(m68k.cycles);
}

void genrecomp_vdp_run_scanline(int line) {
    render_line(line);
}

void genrecomp_vdp_handle_vblank(void) {
    status |= 0x08;
    vint_pending = 1;
}

const uint8_t *genrecomp_vdp_get_framebuffer(void) {
    return bitmap.data;
}
