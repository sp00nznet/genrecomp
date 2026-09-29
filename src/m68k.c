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
#include "genrecomp/func_table.h"
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
     * 4. Dispatch to handler via func_table_call
     *
     * For TRAP instructions (vectors 32-47), the handler is called
     * directly. The handler saves/restores registers and ends with RTE,
     * which pops the saved PC and SR from the stack.
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
    uint32_t handler_addr = bus_read32((uint32_t)vector * 4);
    g_m68k.pc = handler_addr;

    /* Update SSP */
    g_m68k.ssp = g_m68k.a[7];

    /* Dispatch to the handler function.
     * The recompiled handler ends with "return; // RTE" which just returns
     * here. The original RTE would pop SR and PC from the supervisor stack,
     * but since we called via C function call, we need to clean up the
     * exception frame we pushed above. */
    func_table_call(handler_addr);

    /* Pop the exception frame: SR (word) then PC (long) */
    uint16_t restored_sr = bus_read16(g_m68k.a[7]);
    g_m68k.a[7] += 2;
    g_m68k.pc = bus_read32(g_m68k.a[7]);
    g_m68k.a[7] += 4;
    m68k_set_sr(restored_sr);
    g_m68k.ssp = g_m68k.a[7];
}

uint8_t g_m68k_irq_pending;  /* highest requested level not yet taken */

void recomp_m68k_interrupt(uint8_t level) {
    /* Masked (SR I2-I0 >= level, level 7 excepted): remember it; the bus
     * clock takes it as soon as the mask drops, like the hardware does. */
    if (level <= g_m68k.int_mask && level != 7) {
        if (level > g_m68k_irq_pending) g_m68k_irq_pending = level;
        return;
    }
    if (g_m68k_irq_pending <= level) g_m68k_irq_pending = 0;

    /* The hardware stacks PC and SR and RTE restores them; the handler
     * saves any registers it uses. Recompiled code is interrupted in the
     * middle of a bus access, possibly between setting flags and branching
     * on them, so everything is restored here, not only what RTE would. */
    M68kContext saved = g_m68k;
    g_m68k.flag_S = true;
    g_m68k.int_mask = level;
    func_table_call(bus_read32((uint32_t)(24 + level) * 4));
    g_m68k = saved;
}
