/*
 * Recompiled function dispatch table.
 * Hash table mapping 24-bit M68K addresses to native C function pointers.
 */

#include "genrecomp/func_table.h"
#include "genrecomp/m68k.h"
#include "genrecomp/bus.h"
#include <string.h>
#include <stdio.h>

#define FUNC_TABLE_SIZE  4096  /* Must be power of 2 */

typedef struct FuncEntry {
    uint32_t    m68k_addr;
    gen_func_t  func;
    bool        occupied;
} FuncEntry;

static FuncEntry s_table[FUNC_TABLE_SIZE];

void func_table_init(void) {
    memset(s_table, 0, sizeof(s_table));
}

static uint32_t hash_addr(uint32_t addr) {
    addr = ((addr >> 16) ^ addr) * 0x45D9F3B;
    addr = ((addr >> 16) ^ addr) * 0x45D9F3B;
    addr = (addr >> 16) ^ addr;
    return addr & (FUNC_TABLE_SIZE - 1);
}

void func_table_register(uint32_t m68k_addr, gen_func_t func) {
    uint32_t idx = hash_addr(m68k_addr);
    for (uint32_t i = 0; i < FUNC_TABLE_SIZE; i++) {
        uint32_t slot = (idx + i) & (FUNC_TABLE_SIZE - 1);
        if (!s_table[slot].occupied || s_table[slot].m68k_addr == m68k_addr) {
            s_table[slot].m68k_addr = m68k_addr;
            s_table[slot].func = func;
            s_table[slot].occupied = true;
            return;
        }
    }
    fprintf(stderr, "func_table: table full, cannot register $%06X\n", m68k_addr);
}

gen_func_t func_table_lookup(uint32_t m68k_addr) {
    uint32_t idx = hash_addr(m68k_addr);
    for (uint32_t i = 0; i < FUNC_TABLE_SIZE; i++) {
        uint32_t slot = (idx + i) & (FUNC_TABLE_SIZE - 1);
        if (!s_table[slot].occupied) return NULL;
        if (s_table[slot].m68k_addr == m68k_addr) return s_table[slot].func;
    }
    return NULL;
}

static int s_miss_count = 0;
static uint32_t s_last_miss = 0;
static int s_call_depth = 0;
static int s_max_depth = 0;
static uint32_t s_stack[512];  /* shadow call stack of M68K addresses */

void func_table_dump_stack(FILE *f) {
    fprintf(f, "call stack (%d):", s_call_depth);
    for (int i = 0; i < s_call_depth && i < 512; i++) fprintf(f, " $%06X", s_stack[i]);
    fprintf(f, "\n");
}

static uint32_t s_tail;  /* pending tail call, 0 = none */

void func_table_tail(uint32_t m68k_addr) {
    s_tail = m68k_addr;
}

/* Return address each frame's JSR pushed (0: entered some other way) */
static uint32_t s_expect[512];

bool func_table_jsr(uint32_t m68k_addr, uint32_t ret) {
    g_m68k.a[7] -= 4;
    bus_write32(g_m68k.a[7], ret);
    return func_table_call_expecting(m68k_addr, ret);
}

void func_table_rts(void) {
    /* RTS pops the real return address. If it isn't the one this frame's
     * JSR pushed, the code arranged its own continuation (PEA + BRA,
     * push + JMP, a rewritten return address): go there, in this frame,
     * rather than back to the C caller. */
    uint32_t r = bus_read32(g_m68k.a[7]) & 0xFFFFFF;
    g_m68k.a[7] += 4;
    int d = s_call_depth - 1;
    uint32_t e = (d >= 0 && d < 512) ? s_expect[d] : 0;
    if (e && r != e) {
        if (func_table_lookup(r)) s_tail = r;
        else {
            /* Mid-function return points (a task switcher returning into
             * another task's saved PC) aren't entry points: fall back to a
             * plain return, which is what the C call structure implies. */
            static int s_warned;
            if (s_warned++ < 10) {
                fprintf(stderr, "func_table: RTS to $%06X (expected $%06X, SP=$%08X) has no function; returning, ",
                        r, e, g_m68k.a[7] - 4);
                func_table_dump_stack(stderr);
            }
        }
    }
}

bool func_table_call(uint32_t m68k_addr) {
    return func_table_call_expecting(m68k_addr, 0);
}

bool func_table_call_expecting(uint32_t m68k_addr, uint32_t ret) {
    s_call_depth++;
    if (s_call_depth <= 512) s_expect[s_call_depth - 1] = ret;
    if (s_call_depth > s_max_depth) {
        s_max_depth = s_call_depth;
        if (s_max_depth <= 50 || (s_max_depth % 100 == 0)) {
            fprintf(stderr, "[depth] new max call depth: %d (calling $%06X)\n", s_max_depth, m68k_addr);
        }
    }
    if (s_call_depth > 500) {
        fprintf(stderr, "[depth] ABORT: call depth %d at $%06X — likely infinite recursion\n", s_call_depth, m68k_addr);
        s_call_depth--;
        return false;
    }

    /* Trampoline: a function that ends in a tail jump (JMP, BRA or
     * fall-through into another function) records it with
     * func_table_tail() and returns, and the jump runs here in the same
     * frame. M68K code loops through such jumps freely; as nested C calls
     * they would grow the native stack every iteration. */
    bool found = true;
    do {
        s_tail = 0;
        gen_func_t fn = func_table_lookup(m68k_addr);
        if (!fn) {
            if (s_miss_count < 20 && m68k_addr != s_last_miss) {
                fprintf(stderr, "func_table: no function at $%06X (SP=$%08X), ", m68k_addr, g_m68k.a[7]);
                func_table_dump_stack(stderr);  /* the caller is the last entry */
                s_miss_count++;
                s_last_miss = m68k_addr;
            }
            found = false;
            break;
        }
        s_stack[s_call_depth - 1] = m68k_addr;
        fn();
        m68k_addr = s_tail;
    } while (m68k_addr);

    s_call_depth--;
    return found;
}
