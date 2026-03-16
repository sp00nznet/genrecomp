#ifndef GENRECOMP_DEBUG_H
#define GENRECOMP_DEBUG_H

/*
 * Debug framework — tracing, breakpoints, watchpoints, GDB stub.
 *
 * All instrumentation is gated behind GENRECOMP_DEBUG at compile time.
 * When not defined, all macros expand to nothing (zero overhead).
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef GENRECOMP_DEBUG

/* Trace function entry (call at start of every recompiled function) */
#define DEBUG_FUNC_ENTRY(addr) debug_trace_func(addr, __func__)

/* Check breakpoint (call at start of every recompiled function) */
#define DEBUG_CHECK_BP(addr) \
    do { if (debug_check_breakpoint(addr)) debug_break(); } while(0)

/* Initialize debug system */
bool debug_init(void);
void debug_shutdown(void);

/* Tracing */
void debug_trace_func(uint32_t m68k_addr, const char *native_name);
void debug_set_trace_enabled(bool enabled);

/* Breakpoints */
bool debug_add_breakpoint(uint32_t m68k_addr);
bool debug_remove_breakpoint(uint32_t m68k_addr);
bool debug_check_breakpoint(uint32_t m68k_addr);
void debug_break(void);

/* Memory watchpoints */
bool debug_add_watchpoint(uint32_t addr, uint32_t size, bool on_write);
bool debug_remove_watchpoint(uint32_t addr);
void debug_check_watchpoint(uint32_t addr, uint32_t size, bool is_write);

/* GDB remote serial protocol stub */
bool debug_gdb_start(uint16_t port);
void debug_gdb_poll(void);
void debug_gdb_stop(void);

/* CPU state dump */
void debug_dump_cpu(void);
void debug_dump_memory(uint32_t addr, uint32_t size);

#else /* !GENRECOMP_DEBUG */

#define DEBUG_FUNC_ENTRY(addr) ((void)0)
#define DEBUG_CHECK_BP(addr)   ((void)0)

#endif /* GENRECOMP_DEBUG */

#endif /* GENRECOMP_DEBUG_H */
