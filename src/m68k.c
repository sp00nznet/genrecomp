/*
 * Recomp CPU state — the M68K registers your recompiled code uses.
 *
 * This is completely separate from Genesis Plus GX's M68K emulation.
 * In a recomp, YOUR native C code IS the CPU. This struct just holds
 * the register file so recompiled instructions can read/write D0-D7,
 * A0-A7, flags, etc.
 */

#include "genrecomp/m68k.h"
#include "genrecomp/bus.h"
#include <string.h>
#include <stdio.h>

M68kContext g_m68k;

void recomp_m68k_reset(void) {
    memset(&g_m68k, 0, sizeof(g_m68k));

    /* Power-on defaults: supervisor mode, all interrupts masked */
    g_m68k.flag_S = true;
    g_m68k.int_mask = 7;

    /* Read initial SSP and PC from vector table (ROM addresses 0x000000-0x000007) */
    g_m68k.ssp = bus_read32(0x000000);
    g_m68k.pc  = bus_read32(0x000004);
    g_m68k.a[7] = g_m68k.ssp;

    g_m68k.cycles = 0;
    g_m68k.target_cycles = 0;
}

void recomp_m68k_exception(uint8_t vector) {
    /*
     * M68K exception processing:
     * 1. Switch to supervisor mode
     * 2. Push PC and SR on supervisor stack
     * 3. Load new PC from vector table
     *
     * In recompiled code, most exceptions won't fire (no illegal
     * instructions, no bus errors). This is mainly for VBlank/HBlank
     * interrupt handling and TRAP instructions.
     */
    uint16_t old_sr = m68k_get_sr();

    /* Enter supervisor mode */
    if (!g_m68k.flag_S) {
        g_m68k.usp = g_m68k.a[7];
        g_m68k.a[7] = g_m68k.ssp;
        g_m68k.flag_S = true;
    }

    /* Push PC (long) then SR (word) onto supervisor stack */
    g_m68k.a[7] -= 4;
    bus_write32(g_m68k.a[7], g_m68k.pc);
    g_m68k.a[7] -= 2;
    bus_write16(g_m68k.a[7], old_sr);

    /* Load new PC from vector table */
    g_m68k.pc = bus_read32((uint32_t)vector * 4);

    /* Update SSP */
    g_m68k.ssp = g_m68k.a[7];
}
