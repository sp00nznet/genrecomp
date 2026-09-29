#ifndef GENRECOMP_BUS_H
#define GENRECOMP_BUS_H

/*
 * Memory bus — the bridge between your recompiled code and real Genesis hardware.
 *
 * When your recompiled function does:
 *   MOVE.W D0, $C00004    (write to VDP control port)
 *
 * It becomes:
 *   bus_write16(0xC00004, (uint16_t)g_m68k.d[0]);
 *
 * That write goes through Genesis Plus GX's full memory bus to the real
 * VDP, which updates state exactly like the original hardware would.
 *
 * The Genesis M68K has a flat 24-bit address space:
 *   $000000-$3FFFFF  ROM (up to 4MB)
 *   $A00000-$A0FFFF  Z80 address space
 *   $A10000-$A1001F  I/O ports (controllers, etc.)
 *   $A11100-$A11101  Z80 bus request
 *   $A11200-$A11201  Z80 reset
 *   $C00000-$C00003  VDP data port
 *   $C00004-$C00007  VDP control port
 *   $C00008-$C0000F  VDP HV counter
 *   $C00011          PSG output
 *   $FF0000-$FFFFFF  M68K RAM (64KB, mirrored)
 */

#include <stdint.h>
#include <stdbool.h>

/* Flat 24-bit address space reads/writes (big-endian, like real hardware) */
uint8_t  bus_read8(uint32_t addr);
uint16_t bus_read16(uint32_t addr);
uint32_t bus_read32(uint32_t addr);
void     bus_write8(uint32_t addr, uint8_t val);
void     bus_write16(uint32_t addr, uint16_t val);
void     bus_write32(uint32_t addr, uint32_t val);

/* Direct M68K RAM access (faster than bus routing for stack/local operations) */
/* Offset is 0x0000-0xFFFF within the 64KB RAM */
uint8_t  bus_ram_read8(uint16_t offset);
void     bus_ram_write8(uint16_t offset, uint8_t val);
uint16_t bus_ram_read16(uint16_t offset);
void     bus_ram_write16(uint16_t offset, uint16_t val);
uint32_t bus_ram_read32(uint16_t offset);
void     bus_ram_write32(uint16_t offset, uint32_t val);

/* Get pointer to M68K RAM (64KB) for bulk access */
uint8_t *bus_get_ram(void);

/* Write RAM + VDP state snapshot (see bus.c for layout) */
#include <stdio.h>
void bus_dump_state(FILE *f);

/* Get pointer to ROM data */
const uint8_t *bus_get_rom(uint32_t *size_out);

/* VBlank callback — called automatically when bus cycle simulation
 * crosses the VBlank boundary (scanline reaches active display height).
 * This allows VBlank-driven code (counters, interrupt handlers) to
 * execute during tight polling loops in recompiled code. */
typedef void (*bus_vblank_callback_t)(void);
void bus_set_vblank_callback(bus_vblank_callback_t cb);

#endif /* GENRECOMP_BUS_H */
