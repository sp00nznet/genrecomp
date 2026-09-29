/*
 * Runtime self-check: the two recompiled-code semantics that broke real
 * titles silently (see docs/recomp-runtime.md).
 *   1. .b/.w ALU macros must leave the untouched upper bits of a register.
 *   2. func_table_tail() must run chains of tail jumps without nesting.
 */

#include <genrecomp/m68k.h>
#include <genrecomp/func_table.h>
#include <stdio.h>
#include <stdlib.h>

/* always on: Release builds define NDEBUG */
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static int s_hops;

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
