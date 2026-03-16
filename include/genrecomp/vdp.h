#ifndef GENRECOMP_VDP_H
#define GENRECOMP_VDP_H

/*
 * VDP (Video Display Processor) access API.
 *
 * Wraps Genesis Plus GX's VDP for register access, VRAM/CRAM/VSRAM
 * operations, and rendering control.
 *
 * All functions prefixed with genrecomp_vdp_ to avoid collision with
 * GenPlusGX's internal vdp_ functions.
 */

#include <stdint.h>
#include <stdbool.h>

/* VDP initialization and shutdown */
bool genrecomp_vdp_init(void);
void genrecomp_vdp_shutdown(void);

/* Register access */
void     genrecomp_vdp_write_reg(uint8_t reg, uint8_t val);
uint8_t  genrecomp_vdp_read_reg(uint8_t reg);

/* Data port access (VRAM/CRAM/VSRAM depending on access mode) */
void     genrecomp_vdp_write_data(uint16_t val);
uint16_t genrecomp_vdp_read_data(void);

/* Control port access */
void     genrecomp_vdp_write_ctrl(uint16_t val);
uint16_t genrecomp_vdp_read_status(void);

/* HV counter */
uint16_t genrecomp_vdp_read_hv(void);

/* Rendering */
void genrecomp_vdp_run_scanline(int line);
void genrecomp_vdp_handle_vblank(void);

/* Get pointer to rendered framebuffer (320x224 or 256x224, RGBX8888) */
const uint8_t *genrecomp_vdp_get_framebuffer(void);

#endif /* GENRECOMP_VDP_H */
