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

/* GenPlusGX headers */
#include "shared.h"

#include <stdio.h>

/* ================================================================
 * Flat 24-bit address space reads
 *
 * GenPlusGX's memory_map[256] divides the 24-bit space into 256
 * x 64KB regions. Each region has either a base pointer (for ROM/RAM)
 * or read8/read16 handler functions (for I/O-mapped hardware).
 * ================================================================ */

uint8_t bus_read8(uint32_t addr) {
    addr &= 0xFFFFFF;
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
    unsigned int region = (addr >> 16) & 0xFF;
    cpu_memory_map *map = &m68k.memory_map[region];

    if (map->write8) {
        map->write8(addr, val);
        return;
    }
    if (map->base) {
        WRITE_BYTE(map->base, addr & 0xFFFF, val);
    }
}

void bus_write16(uint32_t addr, uint16_t val) {
    addr &= 0xFFFFFF;
    unsigned int region = (addr >> 16) & 0xFF;
    cpu_memory_map *map = &m68k.memory_map[region];

    if (map->write16) {
        map->write16(addr, val);
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
