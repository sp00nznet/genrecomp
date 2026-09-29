/*
 * Recompiled function dispatch table.
 * Hash table mapping 24-bit M68K addresses to native C function pointers.
 */

#include "genrecomp/func_table.h"
#include "genrecomp/m68k.h"
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

bool func_table_call(uint32_t m68k_addr) {
    gen_func_t fn = func_table_lookup(m68k_addr);
    if (fn) {
        s_call_depth++;
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
        s_stack[s_call_depth - 1] = m68k_addr;
        fn();
        s_call_depth--;
        return true;
    }
    if (s_miss_count < 20 && m68k_addr != s_last_miss) {
        fprintf(stderr, "func_table: no function at $%06X (SP=$%08X)\n", m68k_addr, g_m68k.a[7]);
        s_miss_count++;
        s_last_miss = m68k_addr;
    }
    return false;
}
