/*
 * Memory bus — routes recompiled code memory accesses to Genesis Plus GX
 * hardware (VDP, sound, Z80, I/O, cartridge, RAM).
 *
 * Your recompiled code calls bus_read8/bus_write8 with a 24-bit M68K
 * address. We route it through GenPlusGX's memory map system, which
 * dispatches to the correct hardware component (VDP, I/O, Z80, etc.).
 *
 * For ROM/RAM regions with direct base pointers, we read/write directly.
 * For I/O-mapped regions (VDP, sound, etc.), we call the handler functions.
 */

#include "genrecomp/bus.h"
#include "genrecomp/m68k.h"
#include "genrecomp/platform.h"

/* GenPlusGX headers */
#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include "genrecomp/func_table.h"

/* ================================================================
 * Cycle simulation for I/O-mapped hardware
 *
 * GenPlusGX's VDP, I/O, and other hardware handlers use m68k.cycles
 * to determine timing (e.g. VDP V/H counter reads). Since recompiled
 * code doesn't execute the M68K interpreter, we simulate cycle
 * advancement on each bus access. This keeps hardware counters
 * (especially the VDP HV counter) progressing naturally.
 *
 * MCYCLES_PER_LINE is 3420 master clocks per scanline.
 * A typical M68K memory access takes ~4 cycles = ~28 master clocks.
 *
 * We also advance v_counter when enough cycles have accumulated for
 * a full scanline, since GPGX's vdp_hvc_r() only looks 1 line ahead.
 * ================================================================ */
#define BUS_CYCLES_PER_ACCESS 28

static bus_vblank_callback_t s_vblank_cb = NULL;
static bool s_in_tick = false;  /* reentrancy guard */

void bus_set_vblank_callback(bus_vblank_callback_t cb) {
    s_vblank_cb = cb;
}

static uint32_t s_last_addr;  /* previous access, for the warning below */

static void bus_tick_cycles(void) {
    /* GPGX handlers may move m68k.cycles backwards: an access to an
     * unmapped area "locks up" the CPU by setting it to m68k.cycle_end,
     * which is 0 because m68k_run() never runs. Unsigned, that makes
     * (cycles - mcycles_vdp) huge and the line loop below races through
     * thousands of frames inside one access. Time never runs backwards. */
    if (m68k.cycles < mcycles_vdp) {
        static int s_warned = 0;
        if (s_warned++ < 8)
            fprintf(stderr, "bus: cycle counter went backwards (last access $%06X) -- "
                            "unmapped access?\n", s_last_addr);
        m68k.cycles = mcycles_vdp;
    }
    m68k.cycles += BUS_CYCLES_PER_ACCESS;

    /* Sync GPGX internal PC with recomp PC.
     * Some GPGX handlers (e.g. Z80 BUSACK) read m68k.pc for
     * "prefetched bus data" on unused bits. Keep them in sync. */
    m68k.pc = g_m68k.pc;

    /* Auto-clear DMA busy flag.
     * In recompiled code, VDP DMA is triggered by register writes but
     * never "processed" by the emulator's DMA engine. Clear the DMA busy
     * bit in the VDP status register so games don't spin-wait forever.
     * DMA effectively completes instantly in recompiled code. */
    status &= ~0x0002;

    /* Avoid reentrancy: VBlank callback does bus accesses which re-enter */
    if (s_in_tick) return;
    s_in_tick = true;

    /* An interrupt held back by the SR mask is taken once the mask drops */
    if (g_m68k_irq_pending > g_m68k.int_mask)
        recomp_m68k_interrupt(g_m68k_irq_pending);

    /* Advance the scanline clock whenever a full line of cycles has
     * accumulated, mirroring system_frame_gen(): the Z80 runs to the end
     * of each line (so its sound driver consumes the 68K's commands and
     * plays), VBlank raises the Z80 interrupt and fires the recomp's
     * VBlank callback, and at the end of the frame the sound chips are
     * mixed and every cycle counter is rebased to the new frame. */
    while ((m68k.cycles - mcycles_vdp) >= MCYCLES_PER_LINE) {
        mcycles_vdp += MCYCLES_PER_LINE;
        v_counter++;

        int vblank_line = bitmap.viewport.h > 0 ? bitmap.viewport.h : 224;
        bool vblank_now = (v_counter == vblank_line);
        if (vblank_now) {
            status |= 0x08;
            Z80.irq_state = ASSERT_LINE;
        }

        if (zstate == 1) z80_run(mcycles_vdp);
        else Z80.cycles = mcycles_vdp;
        Z80.irq_state = CLEAR_LINE;

        if (vblank_now && s_vblank_cb) s_vblank_cb();

        if (v_counter >= lines_per_frame) {
            static int16_t s_samples[4096];
            int n = audio_update(s_samples);
            if (n > 0) platform_queue_audio(s_samples, n);
            v_counter = 0;
            status &= ~0x08;
            /* Rebase like system_frame_gen. The FIFO timestamps matter:
             * a full FIFO stalls the CPU by setting m68k.cycles to one,
             * and a stale one from last frame jumps hundreds of lines. */
            input_end_frame(mcycles_vdp);
            m68k.cycles -= mcycles_vdp;
            Z80.cycles -= mcycles_vdp;
            mcycles_vdp = 0;
            dma_endCycles = 0;
            fifo_cycles[0] = fifo_cycles[1] = fifo_cycles[2] = fifo_cycles[3] = 0;
        }
    }

    s_in_tick = false;
}

/* ================================================================
 * Instant VDP DMA
 *
 * GPGX spreads a DMA over scanlines: vdp_dma_update() does one line's
 * worth and system_frame_gen() does the rest. Recompiled code never runs
 * system_frame_gen, so a large DMA would stay pending forever, and while
 * a 68K-bus DMA is pending GPGX latches (and here, loses) further control
 * port writes, wedging the VDP. So after any VDP write we run the DMA to
 * completion, as if it happened between two instructions. Timing-exact
 * DMA (mid-frame raster effects fed by DMA) is out of scope.
 * ================================================================ */
static void bus_finish_dma(void) {
    if (!dma_length) return;
    unsigned int saved_cycles = m68k.cycles;
    uint16 saved_vblank = status & 0x08;
    status |= 0x08;  /* blanking rate: largest chunk per call */
    for (int guard = 0; dma_length && guard < 64; guard++)
        vdp_dma_update(0);
    status = (status & ~0x0A) | saved_vblank;
    m68k.cycles = saved_cycles;  /* 68K-bus DMA stalls the CPU; we don't */
}

/* ================================================================
 * Write watchpoint: GENRECOMP_WATCH=FFFEDE (hex, 24-bit) logs every write
 * that touches that byte, with the shadow call stack. For "who overwrote
 * this" questions, e.g. a return address found zeroed at RTS.
 * ================================================================ */
static void bus_watch(uint32_t addr, int size, uint32_t val) {
    static int s_init;
    static uint32_t s_watch;
    if (!s_init) {
        const char *w = getenv("GENRECOMP_WATCH");
        s_watch = w ? (uint32_t)strtoul(w, NULL, 16) & 0xFFFFFF : 0xFFFFFFFF;
        s_init = 1;
    }
    if (s_watch - addr < (uint32_t)size) {
        fprintf(stderr, "watch: write%d $%06X = $%X, ", size * 8, addr, val);
        func_table_dump_stack(stderr);
    }
}

/* ================================================================
 * Flat 24-bit address space reads
 *
 * GenPlusGX's memory_map[256] divides the 24-bit space into 256
 * x 64KB regions. Each region has either a base pointer (for ROM/RAM)
 * or read8/read16 handler functions (for I/O-mapped hardware).
 * ================================================================ */

uint8_t bus_read8(uint32_t addr) {
    addr &= 0xFFFFFF;
    bus_tick_cycles();
    s_last_addr = addr;
    unsigned int region = (addr >> 16) & 0xFF;
    cpu_memory_map *map = &m68k.memory_map[region];

    if (map->read8) {
        return (uint8_t)map->read8(addr);
    }
    if (map->base) {
        return READ_BYTE(map->base, addr & 0xFFFF);
    }
    return 0xFF;
}

uint16_t bus_read16(uint32_t addr) {
    addr &= 0xFFFFFF;
    bus_tick_cycles();
    s_last_addr = addr;
    unsigned int region = (addr >> 16) & 0xFF;
    cpu_memory_map *map = &m68k.memory_map[region];

    if (map->read16) {
        return (uint16_t)map->read16(addr);
    }
    if (map->base) {
        return *(uint16 *)(map->base + (addr & 0xFFFF));
    }
    return 0xFFFF;
}

uint32_t bus_read32(uint32_t addr) {
    uint16_t hi = bus_read16(addr);
    uint16_t lo = bus_read16(addr + 2);
    return ((uint32_t)hi << 16) | lo;
}

/* ================================================================
 * Flat 24-bit address space writes
 * ================================================================ */

void bus_write8(uint32_t addr, uint8_t val) {
    addr &= 0xFFFFFF;
    bus_watch(addr, 1, val);
    bus_tick_cycles();
    s_last_addr = addr;
    unsigned int region = (addr >> 16) & 0xFF;
    cpu_memory_map *map = &m68k.memory_map[region];

    if (map->write8) {
        map->write8(addr, val);
        if (region >= 0xC0 && region <= 0xDF) bus_finish_dma();
        return;
    }
    if (map->base) {
        WRITE_BYTE(map->base, addr & 0xFFFF, val);
    }
}

void bus_write16(uint32_t addr, uint16_t val) {
    addr &= 0xFFFFFF;
    bus_watch(addr, 2, val);
    bus_tick_cycles();
    s_last_addr = addr;
    unsigned int region = (addr >> 16) & 0xFF;
    cpu_memory_map *map = &m68k.memory_map[region];

    if (map->write16) {
        map->write16(addr, val);
        if (region >= 0xC0 && region <= 0xDF) bus_finish_dma();
        return;
    }
    if (map->base) {
        *(uint16 *)(map->base + (addr & 0xFFFF)) = val;
    }
}

void bus_write32(uint32_t addr, uint32_t val) {
    bus_write16(addr, (uint16_t)(val >> 16));
    bus_write16(addr + 2, (uint16_t)(val & 0xFFFF));
}

/* ================================================================
 * Direct RAM access (bypass memory map for speed)
 *
 * GenPlusGX stores M68K work RAM in the global `work_ram[0x10000]`.
 * These functions access it directly for stack/local operations.
 * ================================================================ */

uint8_t bus_ram_read8(uint16_t offset) {
    return READ_BYTE(work_ram, offset);
}

void bus_ram_write8(uint16_t offset, uint8_t val) {
    WRITE_BYTE(work_ram, offset, val);
}

uint16_t bus_ram_read16(uint16_t offset) {
    return *(uint16 *)(work_ram + offset);
}

void bus_ram_write16(uint16_t offset, uint16_t val) {
    *(uint16 *)(work_ram + offset) = val;
}

uint32_t bus_ram_read32(uint16_t offset) {
    return ((uint32_t)bus_ram_read16(offset) << 16) | bus_ram_read16(offset + 2);
}

void bus_ram_write32(uint16_t offset, uint32_t val) {
    bus_ram_write16(offset, (uint16_t)(val >> 16));
    bus_ram_write16(offset + 2, (uint16_t)(val & 0xFFFF));
}

void bus_dump_state(FILE *f) {
    /* Layout: work RAM 64K | VDP regs 32 | CRAM 128 | VSRAM 128 | VRAM 64K.
     * RAM is in GPGX's native (LSB_FIRST byte-swapped) order. */
    fwrite(work_ram, 1, 0x10000, f);
    fwrite(reg, 1, 0x20, f);
    fwrite(cram, 1, 0x80, f);
    fwrite(vsram, 1, 0x80, f);
    fwrite(vram, 1, 0x10000, f);
}

uint8_t *bus_get_ram(void) {
    return work_ram;
}

const uint8_t *bus_get_rom(uint32_t *size_out) {
    /* ROM is mapped starting at region 0x00 of the memory map */
    if (size_out) {
        *size_out = rominfo.romend + 1;
    }
    if (m68k.memory_map[0].base) {
        return m68k.memory_map[0].base;
    }
    return NULL;
}
