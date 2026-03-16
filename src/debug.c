/*
 * Debug framework — tracing, breakpoints, watchpoints, GDB stub.
 * Only compiled when GENRECOMP_DEBUG is defined.
 */

#ifdef GENRECOMP_DEBUG

#include "genrecomp/debug.h"
#include "genrecomp/m68k.h"
#include <stdio.h>
#include <string.h>

#define MAX_BREAKPOINTS  256
#define MAX_WATCHPOINTS  64

static uint32_t s_breakpoints[MAX_BREAKPOINTS];
static int s_bp_count = 0;

typedef struct Watchpoint {
    uint32_t addr;
    uint32_t size;
    bool     on_write;
    bool     active;
} Watchpoint;

static Watchpoint s_watchpoints[MAX_WATCHPOINTS];
static int s_wp_count = 0;

static bool s_trace_enabled = false;
static bool s_debug_initialized = false;

bool debug_init(void) {
    s_bp_count = 0;
    s_wp_count = 0;
    s_trace_enabled = false;
    memset(s_watchpoints, 0, sizeof(s_watchpoints));
    s_debug_initialized = true;
    printf("debug: initialized\n");
    return true;
}

void debug_shutdown(void) {
    s_debug_initialized = false;
}

void debug_trace_func(uint32_t m68k_addr, const char *native_name) {
    if (s_trace_enabled) {
        printf("[TRACE] $%06X -> %s\n", m68k_addr, native_name);
    }
}

void debug_set_trace_enabled(bool enabled) {
    s_trace_enabled = enabled;
}

bool debug_add_breakpoint(uint32_t m68k_addr) {
    if (s_bp_count >= MAX_BREAKPOINTS) return false;
    s_breakpoints[s_bp_count++] = m68k_addr;
    printf("debug: breakpoint added at $%06X\n", m68k_addr);
    return true;
}

bool debug_remove_breakpoint(uint32_t m68k_addr) {
    for (int i = 0; i < s_bp_count; i++) {
        if (s_breakpoints[i] == m68k_addr) {
            s_breakpoints[i] = s_breakpoints[--s_bp_count];
            return true;
        }
    }
    return false;
}

bool debug_check_breakpoint(uint32_t m68k_addr) {
    for (int i = 0; i < s_bp_count; i++) {
        if (s_breakpoints[i] == m68k_addr) return true;
    }
    return false;
}

void debug_break(void) {
    printf("debug: BREAK at PC=$%06X\n", g_m68k.pc);
    debug_dump_cpu();
    /* TODO Phase 4: Actually halt execution and wait for GDB */
}

bool debug_add_watchpoint(uint32_t addr, uint32_t size, bool on_write) {
    if (s_wp_count >= MAX_WATCHPOINTS) return false;
    s_watchpoints[s_wp_count].addr = addr;
    s_watchpoints[s_wp_count].size = size;
    s_watchpoints[s_wp_count].on_write = on_write;
    s_watchpoints[s_wp_count].active = true;
    s_wp_count++;
    return true;
}

bool debug_remove_watchpoint(uint32_t addr) {
    for (int i = 0; i < s_wp_count; i++) {
        if (s_watchpoints[i].addr == addr && s_watchpoints[i].active) {
            s_watchpoints[i].active = false;
            return true;
        }
    }
    return false;
}

void debug_check_watchpoint(uint32_t addr, uint32_t size, bool is_write) {
    for (int i = 0; i < s_wp_count; i++) {
        if (!s_watchpoints[i].active) continue;
        if (s_watchpoints[i].on_write && !is_write) continue;
        uint32_t wp_end = s_watchpoints[i].addr + s_watchpoints[i].size;
        uint32_t acc_end = addr + size;
        if (addr < wp_end && acc_end > s_watchpoints[i].addr) {
            printf("debug: WATCHPOINT hit at $%06X (%s)\n",
                   addr, is_write ? "write" : "read");
            debug_break();
            return;
        }
    }
}

bool debug_gdb_start(uint16_t port) {
    /* TODO Phase 4: TCP RSP server */
    printf("debug: GDB stub would start on port %u\n", port);
    return true;
}

void debug_gdb_poll(void) {
    /* TODO Phase 4 */
}

void debug_gdb_stop(void) {
    /* TODO Phase 4 */
}

void debug_dump_cpu(void) {
    printf("=== M68K CPU State ===\n");
    printf("D0=%08X D1=%08X D2=%08X D3=%08X\n",
           g_m68k.d[0], g_m68k.d[1], g_m68k.d[2], g_m68k.d[3]);
    printf("D4=%08X D5=%08X D6=%08X D7=%08X\n",
           g_m68k.d[4], g_m68k.d[5], g_m68k.d[6], g_m68k.d[7]);
    printf("A0=%08X A1=%08X A2=%08X A3=%08X\n",
           g_m68k.a[0], g_m68k.a[1], g_m68k.a[2], g_m68k.a[3]);
    printf("A4=%08X A5=%08X A6=%08X A7=%08X\n",
           g_m68k.a[4], g_m68k.a[5], g_m68k.a[6], g_m68k.a[7]);
    printf("PC=%08X SR=%04X USP=%08X SSP=%08X\n",
           g_m68k.pc, m68k_get_sr(), g_m68k.usp, g_m68k.ssp);
    printf("Flags: %c%c%c%c%c  S=%d I=%d\n",
           g_m68k.flag_X ? 'X' : '-',
           g_m68k.flag_N ? 'N' : '-',
           g_m68k.flag_Z ? 'Z' : '-',
           g_m68k.flag_V ? 'V' : '-',
           g_m68k.flag_C ? 'C' : '-',
           g_m68k.flag_S, g_m68k.int_mask);
    printf("Cycles: %lld / %lld\n",
           (long long)g_m68k.cycles, (long long)g_m68k.target_cycles);
}

void debug_dump_memory(uint32_t addr, uint32_t size) {
    /* TODO: call bus_read8 and hex dump */
    printf("debug: memory dump at $%06X, %u bytes (TODO)\n", addr, size);
}

#endif /* GENRECOMP_DEBUG */
