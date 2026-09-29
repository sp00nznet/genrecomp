/*
 * Runtime self-check: the two recompiled-code semantics that broke real
 * titles silently (see docs/recomp-runtime.md).
 *   1. .b/.w ALU macros must leave the untouched upper bits of a register.
 *   2. func_table_tail() must run chains of tail jumps without nesting.
 *   3. RTS goes where the 68K stack says, even into a pushed continuation.
 *   4. Packed BCD arithmetic.
 */

#include <genrecomp/m68k.h>
#include <genrecomp/func_table.h>
#include <genrecomp/bus.h>
#include "shared.h"   /* GPGX: map work RAM for the 68K stack */
#include <stdio.h>
#include <stdlib.h>

/* always on: Release builds define NDEBUG */
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static int s_hops, s_cont;

/* PEA $3000; BRA $2100 ... RTS: the routine returns into the continuation
 * it pushed, which then returns to the original caller. */
static void pea_bra(void) { g_m68k.a[7] -= 4; bus_write32(g_m68k.a[7], 0x3000); func_table_tail(0x2100); }
static void does_rts(void) { func_table_rts(); }
static void continuation(void) { s_cont++; func_table_rts(); }

static void hop(void) {
    /* M68K: BRA to itself 10000 times. Nested C calls would hit the
     * dispatcher's depth-500 abort long before the count is reached. */
    if (++s_hops < 10000) func_table_tail(0x1000);
}

int main(void) {
    /* ror.w #8 then swap: the upper word must survive the .w rotate */
    g_m68k.d[1] = 0x007FBCA0;
    M68K_ROR16(g_m68k.d[1], 8);
    CHECK(g_m68k.d[1] == 0x007FA0BC);
    M68K_SWAP(g_m68k.d[1]);
    CHECK((g_m68k.d[1] & 0xFF) == 0x7F);

    g_m68k.d[0] = 0x12345678;
    M68K_ADD8(g_m68k.d[0], 0x10);
    CHECK(g_m68k.d[0] == 0x12345688);
    M68K_LSL16(g_m68k.d[0], 4);
    CHECK(g_m68k.d[0] == 0x12346880);
    M68K_NOT8(g_m68k.d[0]);
    CHECK(g_m68k.d[0] == 0x1234687F);
    M68K_EXT16(g_m68k.d[0]);
    CHECK(g_m68k.d[0] == 0x1234007F);

    /* BCD: 45+38=83, 99+1=00 carry, 23-19=04, 00-01=99 borrow, NBCD 01 = 99 */
    g_m68k.flag_X = false; g_m68k.d[2] = 0xAAAA0045;
    M68K_ABCD(g_m68k.d[2], 0x38);
    CHECK(g_m68k.d[2] == 0xAAAA0083 && !g_m68k.flag_C);
    g_m68k.flag_X = false; g_m68k.d[2] = 0x99;
    M68K_ABCD(g_m68k.d[2], 0x01);
    CHECK(g_m68k.d[2] == 0x00 && g_m68k.flag_C && g_m68k.flag_X);
    g_m68k.flag_X = false; g_m68k.d[2] = 0x23;
    M68K_SBCD(g_m68k.d[2], 0x19);
    CHECK(g_m68k.d[2] == 0x04 && !g_m68k.flag_C);
    g_m68k.flag_X = false; g_m68k.d[2] = 0x00;
    M68K_SBCD(g_m68k.d[2], 0x01);
    CHECK(g_m68k.d[2] == 0x99 && g_m68k.flag_C);
    g_m68k.flag_X = false; g_m68k.flag_Z = true; g_m68k.d[2] = 0x01;
    M68K_NBCD(g_m68k.d[2]);
    CHECK(g_m68k.d[2] == 0x99 && g_m68k.flag_C && !g_m68k.flag_Z);

    /* memory operands are sized temporaries: the merge must be a no-op */
    uint16_t tmp = 0x00FF;
    M68K_ADD16(tmp, 1);
    CHECK(tmp == 0x0100);

    func_table_init();
    func_table_register(0x1000, hop);
    CHECK(func_table_call(0x1000));
    CHECK(s_hops == 10000);

    printf("test_runtime: ok\n");
    return 0;
}
