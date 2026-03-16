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

#endif /* GENRECOMP_FUNC_TABLE_H */
