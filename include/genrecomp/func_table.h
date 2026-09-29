#ifndef GENRECOMP_FUNC_TABLE_H
#define GENRECOMP_FUNC_TABLE_H

/*
 * Recompiled function dispatch table.
 *
 * Register your recompiled C functions at their original M68K 24-bit
 * addresses, then call them by address. All functions use the global
 * CPU state (g_m68k) and bus_read/bus_write for memory access.
 */

#include <stdint.h>
#include <stdbool.h>

/* All recompiled functions: no args, no return, use globals */
typedef void (*gen_func_t)(void);

void        func_table_init(void);
void        func_table_register(uint32_t m68k_addr, gen_func_t func);
gen_func_t  func_table_lookup(uint32_t m68k_addr);
bool        func_table_call(uint32_t m68k_addr);

/* Tail jump: record the target and return from the current recompiled
 * function; the enclosing func_table_call() runs it without growing the
 * C stack. Generated code: { func_table_tail(0x1234); return; } */
void        func_table_tail(uint32_t m68k_addr);

/* JSR/BSR: push the return address on the 68K stack and call. The frame
 * remembers it, so func_table_rts() can tell a normal return from code that
 * returns somewhere it arranged itself (PEA + BRA, push + JMP). */
bool        func_table_jsr(uint32_t m68k_addr, uint32_t ret);

/* RTS: pop the return address; if it isn't the one this frame's JSR pushed,
 * continue there (as a tail jump) instead of returning. Generated code:
 * { func_table_rts(); return; } */
void        func_table_rts(void);

/* func_table_call with a known return address (used by func_table_jsr) */
bool        func_table_call_expecting(uint32_t m68k_addr, uint32_t ret);

/* Print the shadow call stack (outermost first). Called from a VBlank
 * callback it shows where the game's main thread is blocked. */
#include <stdio.h>
void        func_table_dump_stack(FILE *f);

#endif /* GENRECOMP_FUNC_TABLE_H */
