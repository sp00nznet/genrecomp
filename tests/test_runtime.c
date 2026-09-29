/*
 * Runtime self-check: recompiled-code semantics that broke a real title
 * silently (see docs/recomp-runtime.md).
 *   .b/.w ALU macros must leave the untouched upper bits of a register.
 */

#include <genrecomp/m68k.h>
#include <stdio.h>
#include <stdlib.h>

/* always on: Release builds define NDEBUG */
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

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

    printf("test_runtime: ok\n");
    return 0;
}
