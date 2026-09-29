#ifndef GENRECOMP_M68K_H
#define GENRECOMP_M68K_H

/*
 * Motorola 68000 CPU register state for recompiled code.
 *
 * This struct is what your recompiled functions read/write directly.
 * It is NOT connected to Genesis Plus GX's M68K emulation — the
 * recompiled native code IS the CPU. This struct just holds the
 * register state that the original 68000 code would have used.
 */

#include <stdint.h>
#include <stdbool.h>

typedef struct M68kContext {
    /* Data registers D0-D7 */
    uint32_t d[8];

    /* Address registers A0-A7 (A7 aliases current stack pointer) */
    uint32_t a[8];

    /* Stack pointers (A7 switches between these based on supervisor mode) */
    uint32_t usp;   /* User Stack Pointer */
    uint32_t ssp;   /* Supervisor Stack Pointer */

    /* Program counter (for debugging/exceptions — recompiled code uses native flow) */
    uint32_t pc;

    /* Condition Code Register flags (individual bools for fast branching) */
    bool flag_C;    /* Carry */
    bool flag_V;    /* Overflow */
    bool flag_Z;    /* Zero */
    bool flag_N;    /* Negative */
    bool flag_X;    /* Extend (like carry, but sticky) */

    /* Supervisor state */
    bool flag_S;    /* Supervisor mode */
    uint8_t int_mask; /* Interrupt priority mask (3 bits, 0-7) */

    /* Cycle tracking for M68K/Z80/VDP synchronization */
    int64_t cycles;
    int64_t target_cycles;
} M68kContext;

extern M68kContext g_m68k;

/* ================================================================
 * CCR / SR access helpers
 * ================================================================ */

/* CCR = lower 5 bits of SR: X N Z V C */
static inline uint8_t m68k_get_ccr(void) {
    uint8_t ccr = 0;
    if (g_m68k.flag_C) ccr |= 0x01;
    if (g_m68k.flag_V) ccr |= 0x02;
    if (g_m68k.flag_Z) ccr |= 0x04;
    if (g_m68k.flag_N) ccr |= 0x08;
    if (g_m68k.flag_X) ccr |= 0x10;
    return ccr;
}

static inline void m68k_set_ccr(uint8_t ccr) {
    g_m68k.flag_C = (ccr & 0x01) != 0;
    g_m68k.flag_V = (ccr & 0x02) != 0;
    g_m68k.flag_Z = (ccr & 0x04) != 0;
    g_m68k.flag_N = (ccr & 0x08) != 0;
    g_m68k.flag_X = (ccr & 0x10) != 0;
}

/* SR = supervisor bits (high byte) + CCR (low byte) */
static inline uint16_t m68k_get_sr(void) {
    uint16_t sr = (uint16_t)m68k_get_ccr();
    if (g_m68k.flag_S) sr |= 0x2000;
    sr |= (uint16_t)(g_m68k.int_mask & 0x07) << 8;
    return sr;
}

static inline void m68k_set_sr(uint16_t sr) {
    m68k_set_ccr((uint8_t)(sr & 0xFF));
    g_m68k.flag_S = (sr & 0x2000) != 0;
    g_m68k.int_mask = (uint8_t)((sr >> 8) & 0x07);
}

/* ================================================================
 * Flag update helpers (N and Z for each operand size)
 * ================================================================ */

static inline void m68k_update_nz8(uint8_t val) {
    g_m68k.flag_N = (val & 0x80) != 0;
    g_m68k.flag_Z = (val == 0);
}

static inline void m68k_update_nz16(uint16_t val) {
    g_m68k.flag_N = (val & 0x8000) != 0;
    g_m68k.flag_Z = (val == 0);
}

static inline void m68k_update_nz32(uint32_t val) {
    g_m68k.flag_N = (val & 0x80000000u) != 0;
    g_m68k.flag_Z = (val == 0);
}

/* ================================================================
 * Arithmetic macros
 *
 * These update flags exactly as the real M68K does.
 * dst/src are evaluated once via do{} wrapper.
 * Result is stored back into dst.
 * ================================================================ */

/* --- ADD (8/16/32) --- */
#define M68K_ADD8(dst, src) do { \
    uint8_t _s = (uint8_t)(src); \
    uint8_t _d = (uint8_t)(dst); \
    uint16_t _r = (uint16_t)_d + (uint16_t)_s; \
    uint8_t _res = (uint8_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = (_r > 0xFF); \
    g_m68k.flag_V = ((_d ^ _res) & (_s ^ _res) & 0x80) != 0; \
    m68k_update_nz8(_res); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_res); \
} while(0)

#define M68K_ADD16(dst, src) do { \
    uint16_t _s = (uint16_t)(src); \
    uint16_t _d = (uint16_t)(dst); \
    uint32_t _r = (uint32_t)_d + (uint32_t)_s; \
    uint16_t _res = (uint16_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = (_r > 0xFFFF); \
    g_m68k.flag_V = ((_d ^ _res) & (_s ^ _res) & 0x8000) != 0; \
    m68k_update_nz16(_res); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_res); \
} while(0)

#define M68K_ADD32(dst, src) do { \
    uint32_t _s = (uint32_t)(src); \
    uint32_t _d = (uint32_t)(dst); \
    uint64_t _r = (uint64_t)_d + (uint64_t)_s; \
    uint32_t _res = (uint32_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = (_r > 0xFFFFFFFFu); \
    g_m68k.flag_V = ((_d ^ _res) & (_s ^ _res) & 0x80000000u) != 0; \
    m68k_update_nz32(_res); \
    (dst) = _res; \
} while(0)

/* --- ADDX (add with extend) --- */
#define M68K_ADDX8(dst, src) do { \
    uint8_t _s = (uint8_t)(src); \
    uint8_t _d = (uint8_t)(dst); \
    uint16_t _r = (uint16_t)_d + (uint16_t)_s + (g_m68k.flag_X ? 1u : 0u); \
    uint8_t _res = (uint8_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = (_r > 0xFF); \
    g_m68k.flag_V = ((_d ^ _res) & (_s ^ _res) & 0x80) != 0; \
    g_m68k.flag_N = (_res & 0x80) != 0; \
    if (_res != 0) g_m68k.flag_Z = false; \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_res); \
} while(0)

#define M68K_ADDX16(dst, src) do { \
    uint16_t _s = (uint16_t)(src); \
    uint16_t _d = (uint16_t)(dst); \
    uint32_t _r = (uint32_t)_d + (uint32_t)_s + (g_m68k.flag_X ? 1u : 0u); \
    uint16_t _res = (uint16_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = (_r > 0xFFFF); \
    g_m68k.flag_V = ((_d ^ _res) & (_s ^ _res) & 0x8000) != 0; \
    g_m68k.flag_N = (_res & 0x8000) != 0; \
    if (_res != 0) g_m68k.flag_Z = false; \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_res); \
} while(0)

#define M68K_ADDX32(dst, src) do { \
    uint32_t _s = (uint32_t)(src); \
    uint32_t _d = (uint32_t)(dst); \
    uint64_t _r = (uint64_t)_d + (uint64_t)_s + (g_m68k.flag_X ? 1u : 0u); \
    uint32_t _res = (uint32_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = (_r > 0xFFFFFFFFu); \
    g_m68k.flag_V = ((_d ^ _res) & (_s ^ _res) & 0x80000000u) != 0; \
    g_m68k.flag_N = (_res & 0x80000000u) != 0; \
    if (_res != 0) g_m68k.flag_Z = false; \
    (dst) = _res; \
} while(0)

/* --- SUB (8/16/32) --- */
#define M68K_SUB8(dst, src) do { \
    uint8_t _s = (uint8_t)(src); \
    uint8_t _d = (uint8_t)(dst); \
    uint16_t _r = (uint16_t)_d - (uint16_t)_s; \
    uint8_t _res = (uint8_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = (_d < _s); \
    g_m68k.flag_V = ((_d ^ _s) & (_d ^ _res) & 0x80) != 0; \
    m68k_update_nz8(_res); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_res); \
} while(0)

#define M68K_SUB16(dst, src) do { \
    uint16_t _s = (uint16_t)(src); \
    uint16_t _d = (uint16_t)(dst); \
    uint32_t _r = (uint32_t)_d - (uint32_t)_s; \
    uint16_t _res = (uint16_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = (_d < _s); \
    g_m68k.flag_V = ((_d ^ _s) & (_d ^ _res) & 0x8000) != 0; \
    m68k_update_nz16(_res); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_res); \
} while(0)

#define M68K_SUB32(dst, src) do { \
    uint32_t _s = (uint32_t)(src); \
    uint32_t _d = (uint32_t)(dst); \
    uint64_t _r = (uint64_t)_d - (uint64_t)_s; \
    uint32_t _res = (uint32_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = (_d < _s); \
    g_m68k.flag_V = ((_d ^ _s) & (_d ^ _res) & 0x80000000u) != 0; \
    m68k_update_nz32(_res); \
    (dst) = _res; \
} while(0)

/* --- SUBX (subtract with extend) --- */
#define M68K_SUBX8(dst, src) do { \
    uint8_t _s = (uint8_t)(src); \
    uint8_t _d = (uint8_t)(dst); \
    uint16_t _x = (g_m68k.flag_X ? 1u : 0u); \
    uint16_t _r = (uint16_t)_d - (uint16_t)_s - _x; \
    uint8_t _res = (uint8_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = ((uint16_t)_s + _x > (uint16_t)_d); \
    g_m68k.flag_V = ((_d ^ _s) & (_d ^ _res) & 0x80) != 0; \
    g_m68k.flag_N = (_res & 0x80) != 0; \
    if (_res != 0) g_m68k.flag_Z = false; \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_res); \
} while(0)

#define M68K_SUBX16(dst, src) do { \
    uint16_t _s = (uint16_t)(src); \
    uint16_t _d = (uint16_t)(dst); \
    uint32_t _x = (g_m68k.flag_X ? 1u : 0u); \
    uint32_t _r = (uint32_t)_d - (uint32_t)_s - _x; \
    uint16_t _res = (uint16_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = ((uint32_t)_s + _x > (uint32_t)_d); \
    g_m68k.flag_V = ((_d ^ _s) & (_d ^ _res) & 0x8000) != 0; \
    g_m68k.flag_N = (_res & 0x8000) != 0; \
    if (_res != 0) g_m68k.flag_Z = false; \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_res); \
} while(0)

#define M68K_SUBX32(dst, src) do { \
    uint32_t _s = (uint32_t)(src); \
    uint32_t _d = (uint32_t)(dst); \
    uint64_t _x = (g_m68k.flag_X ? 1u : 0u); \
    uint64_t _r = (uint64_t)_d - (uint64_t)_s - _x; \
    uint32_t _res = (uint32_t)_r; \
    g_m68k.flag_C = g_m68k.flag_X = ((uint64_t)_s + _x > (uint64_t)_d); \
    g_m68k.flag_V = ((_d ^ _s) & (_d ^ _res) & 0x80000000u) != 0; \
    g_m68k.flag_N = (_res & 0x80000000u) != 0; \
    if (_res != 0) g_m68k.flag_Z = false; \
    (dst) = _res; \
} while(0)

/* --- CMP (8/16/32) — like SUB but doesn't store result or update X --- */
#define M68K_CMP8(dst, src) do { \
    uint8_t _s = (uint8_t)(src); \
    uint8_t _d = (uint8_t)(dst); \
    uint8_t _res = _d - _s; \
    g_m68k.flag_C = (_d < _s); \
    g_m68k.flag_V = ((_d ^ _s) & (_d ^ _res) & 0x80) != 0; \
    m68k_update_nz8(_res); \
} while(0)

#define M68K_CMP16(dst, src) do { \
    uint16_t _s = (uint16_t)(src); \
    uint16_t _d = (uint16_t)(dst); \
    uint16_t _res = _d - _s; \
    g_m68k.flag_C = (_d < _s); \
    g_m68k.flag_V = ((_d ^ _s) & (_d ^ _res) & 0x8000) != 0; \
    m68k_update_nz16(_res); \
} while(0)

#define M68K_CMP32(dst, src) do { \
    uint32_t _s = (uint32_t)(src); \
    uint32_t _d = (uint32_t)(dst); \
    uint32_t _res = _d - _s; \
    g_m68k.flag_C = (_d < _s); \
    g_m68k.flag_V = ((_d ^ _s) & (_d ^ _res) & 0x80000000u) != 0; \
    m68k_update_nz32(_res); \
} while(0)

/* --- NEG (8/16/32) — negate: 0 - dst --- */
#define M68K_NEG8(dst) do { \
    uint8_t _d = (uint8_t)(dst); \
    uint8_t _res = (uint8_t)(0 - _d); \
    g_m68k.flag_C = g_m68k.flag_X = (_d != 0); \
    g_m68k.flag_V = (_d == 0x80); \
    m68k_update_nz8(_res); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_res); \
} while(0)

#define M68K_NEG16(dst) do { \
    uint16_t _d = (uint16_t)(dst); \
    uint16_t _res = (uint16_t)(0 - _d); \
    g_m68k.flag_C = g_m68k.flag_X = (_d != 0); \
    g_m68k.flag_V = (_d == 0x8000); \
    m68k_update_nz16(_res); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_res); \
} while(0)

#define M68K_NEG32(dst) do { \
    uint32_t _d = (uint32_t)(dst); \
    uint32_t _res = (uint32_t)(0 - _d); \
    g_m68k.flag_C = g_m68k.flag_X = (_d != 0); \
    g_m68k.flag_V = (_d == 0x80000000u); \
    m68k_update_nz32(_res); \
    (dst) = _res; \
} while(0)

/* --- NEGX (negate with extend) --- */
#define M68K_NEGX8(dst) do { \
    uint8_t _d = (uint8_t)(dst); \
    uint8_t _x = (g_m68k.flag_X ? 1u : 0u); \
    uint8_t _res = (uint8_t)(0 - _d - _x); \
    g_m68k.flag_C = g_m68k.flag_X = (_d != 0 || _x != 0); \
    g_m68k.flag_V = (_res & 0x80) && (_d & 0x80); \
    g_m68k.flag_N = (_res & 0x80) != 0; \
    if (_res != 0) g_m68k.flag_Z = false; \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_res); \
} while(0)

#define M68K_NEGX16(dst) do { \
    uint16_t _d = (uint16_t)(dst); \
    uint16_t _x = (g_m68k.flag_X ? 1u : 0u); \
    uint16_t _res = (uint16_t)(0 - _d - _x); \
    g_m68k.flag_C = g_m68k.flag_X = (_d != 0 || _x != 0); \
    g_m68k.flag_V = (_res & 0x8000) && (_d & 0x8000); \
    g_m68k.flag_N = (_res & 0x8000) != 0; \
    if (_res != 0) g_m68k.flag_Z = false; \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_res); \
} while(0)

#define M68K_NEGX32(dst) do { \
    uint32_t _d = (uint32_t)(dst); \
    uint32_t _x = (g_m68k.flag_X ? 1u : 0u); \
    uint32_t _res = (uint32_t)(0 - _d - _x); \
    g_m68k.flag_C = g_m68k.flag_X = (_d != 0 || _x != 0); \
    g_m68k.flag_V = (_res & 0x80000000u) && (_d & 0x80000000u); \
    g_m68k.flag_N = (_res & 0x80000000u) != 0; \
    if (_res != 0) g_m68k.flag_Z = false; \
    (dst) = _res; \
} while(0)

/* --- MULU / MULS (16x16 -> 32) --- */
#define M68K_MULU(dst, src) do { \
    uint16_t _s = (uint16_t)(src); \
    uint16_t _d = (uint16_t)(dst); \
    uint32_t _res = (uint32_t)_d * (uint32_t)_s; \
    g_m68k.flag_C = false; \
    g_m68k.flag_V = false; \
    m68k_update_nz32(_res); \
    (dst) = _res; \
} while(0)

#define M68K_MULS(dst, src) do { \
    int16_t _s = (int16_t)(src); \
    int16_t _d = (int16_t)(dst); \
    int32_t _res = (int32_t)_d * (int32_t)_s; \
    g_m68k.flag_C = false; \
    g_m68k.flag_V = false; \
    m68k_update_nz32((uint32_t)_res); \
    (dst) = (uint32_t)_res; \
} while(0)

/* --- DIVU / DIVS (32 / 16 -> 16q:16r) --- */
/* Result: low 16 = quotient, high 16 = remainder */
#define M68K_DIVU(dst, src) do { \
    uint16_t _s = (uint16_t)(src); \
    uint32_t _d = (uint32_t)(dst); \
    if (_s == 0) { \
        /* Division by zero — would trigger exception in real hardware */ \
        g_m68k.flag_C = false; \
        break; \
    } \
    uint32_t _q = _d / (uint32_t)_s; \
    uint32_t _rem = _d % (uint32_t)_s; \
    if (_q > 0xFFFF) { \
        /* Overflow */ \
        g_m68k.flag_V = true; \
        g_m68k.flag_C = false; \
    } else { \
        g_m68k.flag_V = false; \
        g_m68k.flag_C = false; \
        uint32_t _res = (_rem << 16) | (_q & 0xFFFF); \
        m68k_update_nz16((uint16_t)_q); \
        (dst) = _res; \
    } \
} while(0)

#define M68K_DIVS(dst, src) do { \
    int16_t _s = (int16_t)(src); \
    int32_t _d = (int32_t)(uint32_t)(dst); \
    if (_s == 0) { \
        g_m68k.flag_C = false; \
        break; \
    } \
    int32_t _q = _d / (int32_t)_s; \
    int32_t _rem = _d % (int32_t)_s; \
    if (_q > 32767 || _q < -32768) { \
        g_m68k.flag_V = true; \
        g_m68k.flag_C = false; \
    } else { \
        g_m68k.flag_V = false; \
        g_m68k.flag_C = false; \
        uint32_t _res = ((uint16_t)_rem << 16) | ((uint16_t)_q & 0xFFFF); \
        m68k_update_nz16((uint16_t)_q); \
        (dst) = _res; \
    } \
} while(0)

/* ================================================================
 * Condition code macros (all 16 M68K conditions)
 * ================================================================ */

#define M68K_CC_T   (1)                                             /* True (always) */
#define M68K_CC_F   (0)                                             /* False (never) */
#define M68K_CC_HI  (!g_m68k.flag_C && !g_m68k.flag_Z)             /* High (unsigned >) */
#define M68K_CC_LS  (g_m68k.flag_C || g_m68k.flag_Z)               /* Low or Same */
#define M68K_CC_CC  (!g_m68k.flag_C)                                /* Carry Clear */
#define M68K_CC_CS  (g_m68k.flag_C)                                 /* Carry Set */
#define M68K_CC_NE  (!g_m68k.flag_Z)                                /* Not Equal */
#define M68K_CC_EQ  (g_m68k.flag_Z)                                 /* Equal */
#define M68K_CC_VC  (!g_m68k.flag_V)                                /* Overflow Clear */
#define M68K_CC_VS  (g_m68k.flag_V)                                 /* Overflow Set */
#define M68K_CC_PL  (!g_m68k.flag_N)                                /* Plus */
#define M68K_CC_MI  (g_m68k.flag_N)                                 /* Minus */
#define M68K_CC_GE  (g_m68k.flag_N == g_m68k.flag_V)               /* Greater or Equal (signed) */
#define M68K_CC_LT  (g_m68k.flag_N != g_m68k.flag_V)               /* Less Than (signed) */
#define M68K_CC_GT  (!g_m68k.flag_Z && (g_m68k.flag_N == g_m68k.flag_V))  /* Greater Than (signed) */
#define M68K_CC_LE  (g_m68k.flag_Z || (g_m68k.flag_N != g_m68k.flag_V))   /* Less or Equal (signed) */

/* ================================================================
 * Logical operation macros (AND, OR, EOR, NOT)
 * ================================================================ */

#define M68K_AND8(dst, src) do { \
    uint8_t _res = (uint8_t)(dst) & (uint8_t)(src); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz8(_res); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_res); \
} while(0)

#define M68K_AND16(dst, src) do { \
    uint16_t _res = (uint16_t)(dst) & (uint16_t)(src); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz16(_res); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_res); \
} while(0)

#define M68K_AND32(dst, src) do { \
    uint32_t _res = (uint32_t)(dst) & (uint32_t)(src); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz32(_res); \
    (dst) = _res; \
} while(0)

#define M68K_OR8(dst, src) do { \
    uint8_t _res = (uint8_t)(dst) | (uint8_t)(src); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz8(_res); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_res); \
} while(0)

#define M68K_OR16(dst, src) do { \
    uint16_t _res = (uint16_t)(dst) | (uint16_t)(src); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz16(_res); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_res); \
} while(0)

#define M68K_OR32(dst, src) do { \
    uint32_t _res = (uint32_t)(dst) | (uint32_t)(src); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz32(_res); \
    (dst) = _res; \
} while(0)

#define M68K_EOR8(dst, src) do { \
    uint8_t _res = (uint8_t)(dst) ^ (uint8_t)(src); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz8(_res); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_res); \
} while(0)

#define M68K_EOR16(dst, src) do { \
    uint16_t _res = (uint16_t)(dst) ^ (uint16_t)(src); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz16(_res); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_res); \
} while(0)

#define M68K_EOR32(dst, src) do { \
    uint32_t _res = (uint32_t)(dst) ^ (uint32_t)(src); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz32(_res); \
    (dst) = _res; \
} while(0)

#define M68K_NOT8(dst) do { \
    uint8_t _res = ~(uint8_t)(dst); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz8(_res); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_res); \
} while(0)

#define M68K_NOT16(dst) do { \
    uint16_t _res = ~(uint16_t)(dst); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz16(_res); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_res); \
} while(0)

#define M68K_NOT32(dst) do { \
    uint32_t _res = ~(uint32_t)(dst); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz32(_res); \
    (dst) = _res; \
} while(0)

/* --- BTST / BSET / BCLR / BCHG --- */
#define M68K_BTST32(val, bit) do { \
    g_m68k.flag_Z = !((uint32_t)(val) & (1u << ((bit) & 31))); \
} while(0)

#define M68K_BTST8(val, bit) do { \
    g_m68k.flag_Z = !((uint8_t)(val) & (1u << ((bit) & 7))); \
} while(0)

#define M68K_BSET32(dst, bit) do { \
    uint32_t _b = 1u << ((bit) & 31); \
    g_m68k.flag_Z = !((uint32_t)(dst) & _b); \
    (dst) |= _b; \
} while(0)

#define M68K_BCLR32(dst, bit) do { \
    uint32_t _b = 1u << ((bit) & 31); \
    g_m68k.flag_Z = !((uint32_t)(dst) & _b); \
    (dst) &= ~_b; \
} while(0)

#define M68K_BCHG32(dst, bit) do { \
    uint32_t _b = 1u << ((bit) & 31); \
    g_m68k.flag_Z = !((uint32_t)(dst) & _b); \
    (dst) ^= _b; \
} while(0)

/* --- TST (test: update N,Z; clear C,V) --- */
#define M68K_TST8(val) do { \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz8((uint8_t)(val)); \
} while(0)

#define M68K_TST16(val) do { \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz16((uint16_t)(val)); \
} while(0)

#define M68K_TST32(val) do { \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz32((uint32_t)(val)); \
} while(0)

/* ================================================================
 * Shift and rotate macros
 *
 * count is masked to 0-63 per M68K spec (shift count from register).
 * For immediate shifts, count is 1-8.
 * ================================================================ */

/* --- LSL (Logical Shift Left) --- */
#define M68K_LSL8(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63); \
    uint8_t _d = (uint8_t)(dst); \
    if (_cnt == 0) { \
        g_m68k.flag_C = false; \
    } else if (_cnt <= 8) { \
        g_m68k.flag_C = g_m68k.flag_X = ((_d >> (8 - _cnt)) & 1) != 0; \
        _d <<= _cnt; \
    } else { \
        g_m68k.flag_C = g_m68k.flag_X = false; \
        _d = 0; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz8(_d); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_d); \
} while(0)

#define M68K_LSL16(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63); \
    uint16_t _d = (uint16_t)(dst); \
    if (_cnt == 0) { \
        g_m68k.flag_C = false; \
    } else if (_cnt <= 16) { \
        g_m68k.flag_C = g_m68k.flag_X = ((_d >> (16 - _cnt)) & 1) != 0; \
        _d <<= _cnt; \
    } else { \
        g_m68k.flag_C = g_m68k.flag_X = false; \
        _d = 0; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz16(_d); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_d); \
} while(0)

#define M68K_LSL32(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63); \
    uint32_t _d = (uint32_t)(dst); \
    if (_cnt == 0) { \
        g_m68k.flag_C = false; \
    } else if (_cnt <= 32) { \
        g_m68k.flag_C = g_m68k.flag_X = ((_d >> (32 - _cnt)) & 1) != 0; \
        _d = (_cnt == 32) ? 0 : (_d << _cnt); \
    } else { \
        g_m68k.flag_C = g_m68k.flag_X = false; \
        _d = 0; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz32(_d); \
    (dst) = _d; \
} while(0)

/* --- LSR (Logical Shift Right) --- */
#define M68K_LSR8(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63); \
    uint8_t _d = (uint8_t)(dst); \
    if (_cnt == 0) { \
        g_m68k.flag_C = false; \
    } else if (_cnt <= 8) { \
        g_m68k.flag_C = g_m68k.flag_X = ((_d >> (_cnt - 1)) & 1) != 0; \
        _d >>= _cnt; \
    } else { \
        g_m68k.flag_C = g_m68k.flag_X = false; \
        _d = 0; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz8(_d); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_d); \
} while(0)

#define M68K_LSR16(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63); \
    uint16_t _d = (uint16_t)(dst); \
    if (_cnt == 0) { \
        g_m68k.flag_C = false; \
    } else if (_cnt <= 16) { \
        g_m68k.flag_C = g_m68k.flag_X = ((_d >> (_cnt - 1)) & 1) != 0; \
        _d >>= _cnt; \
    } else { \
        g_m68k.flag_C = g_m68k.flag_X = false; \
        _d = 0; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz16(_d); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_d); \
} while(0)

#define M68K_LSR32(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63); \
    uint32_t _d = (uint32_t)(dst); \
    if (_cnt == 0) { \
        g_m68k.flag_C = false; \
    } else if (_cnt <= 32) { \
        g_m68k.flag_C = g_m68k.flag_X = ((_d >> (_cnt - 1)) & 1) != 0; \
        _d = (_cnt == 32) ? 0 : (_d >> _cnt); \
    } else { \
        g_m68k.flag_C = g_m68k.flag_X = false; \
        _d = 0; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz32(_d); \
    (dst) = _d; \
} while(0)

/* --- ASR (Arithmetic Shift Right — preserves sign) --- */
#define M68K_ASR8(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63); \
    int8_t _d = (int8_t)(uint8_t)(dst); \
    if (_cnt == 0) { \
        g_m68k.flag_C = false; \
    } else { \
        uint8_t _sc = (_cnt > 8) ? 8 : _cnt; \
        g_m68k.flag_C = g_m68k.flag_X = ((_d >> (_sc - 1)) & 1) != 0; \
        _d >>= _sc; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz8((uint8_t)_d); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)((uint8_t)_d); \
} while(0)

#define M68K_ASR16(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63); \
    int16_t _d = (int16_t)(uint16_t)(dst); \
    if (_cnt == 0) { \
        g_m68k.flag_C = false; \
    } else { \
        uint8_t _sc = (_cnt > 16) ? 16 : _cnt; \
        g_m68k.flag_C = g_m68k.flag_X = ((_d >> (_sc - 1)) & 1) != 0; \
        _d >>= _sc; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz16((uint16_t)_d); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)((uint16_t)_d); \
} while(0)

#define M68K_ASR32(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63); \
    int32_t _d = (int32_t)(uint32_t)(dst); \
    if (_cnt == 0) { \
        g_m68k.flag_C = false; \
    } else { \
        uint8_t _sc = (_cnt > 32) ? 32 : _cnt; \
        g_m68k.flag_C = g_m68k.flag_X = ((_d >> (_sc - 1)) & 1) != 0; \
        _d = (_sc == 32) ? (_d >> 31) : (_d >> _sc); \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz32((uint32_t)_d); \
    (dst) = (uint32_t)_d; \
} while(0)

/* --- ROL (Rotate Left) --- */
#define M68K_ROL8(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 8; \
    uint8_t _d = (uint8_t)(dst); \
    if ((count) & 63) { \
        _d = (_d << _cnt) | (_d >> (8 - _cnt)); \
        g_m68k.flag_C = (_d & 1) != 0; \
    } else { \
        g_m68k.flag_C = false; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz8(_d); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_d); \
} while(0)

#define M68K_ROL16(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 16; \
    uint16_t _d = (uint16_t)(dst); \
    if ((count) & 63) { \
        _d = (_d << _cnt) | (_d >> (16 - _cnt)); \
        g_m68k.flag_C = (_d & 1) != 0; \
    } else { \
        g_m68k.flag_C = false; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz16(_d); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_d); \
} while(0)

#define M68K_ROL32(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 32; \
    uint32_t _d = (uint32_t)(dst); \
    if ((count) & 63) { \
        _d = (_d << _cnt) | (_d >> (32 - _cnt)); \
        g_m68k.flag_C = (_d & 1) != 0; \
    } else { \
        g_m68k.flag_C = false; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz32(_d); \
    (dst) = _d; \
} while(0)

/* --- ROR (Rotate Right) --- */
#define M68K_ROR8(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 8; \
    uint8_t _d = (uint8_t)(dst); \
    if ((count) & 63) { \
        _d = (_d >> _cnt) | (_d << (8 - _cnt)); \
        g_m68k.flag_C = (_d & 0x80) != 0; \
    } else { \
        g_m68k.flag_C = false; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz8(_d); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_d); \
} while(0)

#define M68K_ROR16(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 16; \
    uint16_t _d = (uint16_t)(dst); \
    if ((count) & 63) { \
        _d = (_d >> _cnt) | (_d << (16 - _cnt)); \
        g_m68k.flag_C = (_d & 0x8000) != 0; \
    } else { \
        g_m68k.flag_C = false; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz16(_d); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_d); \
} while(0)

#define M68K_ROR32(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 32; \
    uint32_t _d = (uint32_t)(dst); \
    if ((count) & 63) { \
        _d = (_d >> _cnt) | (_d << (32 - _cnt)); \
        g_m68k.flag_C = (_d & 0x80000000u) != 0; \
    } else { \
        g_m68k.flag_C = false; \
    } \
    g_m68k.flag_V = false; \
    m68k_update_nz32(_d); \
    (dst) = _d; \
} while(0)

/* --- ROXL (Rotate Left through eXtend) --- */
#define M68K_ROXL8(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 9; \
    uint8_t _d = (uint8_t)(dst); \
    for (uint8_t _i = 0; _i < _cnt; _i++) { \
        bool _old_x = g_m68k.flag_X; \
        g_m68k.flag_X = g_m68k.flag_C = (_d & 0x80) != 0; \
        _d = (_d << 1) | (_old_x ? 1u : 0u); \
    } \
    if (!((count) & 63)) g_m68k.flag_C = g_m68k.flag_X; \
    g_m68k.flag_V = false; \
    m68k_update_nz8(_d); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_d); \
} while(0)

#define M68K_ROXL16(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 17; \
    uint16_t _d = (uint16_t)(dst); \
    for (uint8_t _i = 0; _i < _cnt; _i++) { \
        bool _old_x = g_m68k.flag_X; \
        g_m68k.flag_X = g_m68k.flag_C = (_d & 0x8000) != 0; \
        _d = (_d << 1) | (_old_x ? 1u : 0u); \
    } \
    if (!((count) & 63)) g_m68k.flag_C = g_m68k.flag_X; \
    g_m68k.flag_V = false; \
    m68k_update_nz16(_d); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_d); \
} while(0)

#define M68K_ROXL32(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 33; \
    uint32_t _d = (uint32_t)(dst); \
    for (uint8_t _i = 0; _i < _cnt; _i++) { \
        bool _old_x = g_m68k.flag_X; \
        g_m68k.flag_X = g_m68k.flag_C = (_d & 0x80000000u) != 0; \
        _d = (_d << 1) | (_old_x ? 1u : 0u); \
    } \
    if (!((count) & 63)) g_m68k.flag_C = g_m68k.flag_X; \
    g_m68k.flag_V = false; \
    m68k_update_nz32(_d); \
    (dst) = _d; \
} while(0)

/* --- ROXR (Rotate Right through eXtend) --- */
#define M68K_ROXR8(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 9; \
    uint8_t _d = (uint8_t)(dst); \
    for (uint8_t _i = 0; _i < _cnt; _i++) { \
        bool _old_x = g_m68k.flag_X; \
        g_m68k.flag_X = g_m68k.flag_C = (_d & 1) != 0; \
        _d = (_d >> 1) | (_old_x ? 0x80u : 0u); \
    } \
    if (!((count) & 63)) g_m68k.flag_C = g_m68k.flag_X; \
    g_m68k.flag_V = false; \
    m68k_update_nz8(_d); \
    (dst) = ((dst) & ~0xFFu) | (uint8_t)(_d); \
} while(0)

#define M68K_ROXR16(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 17; \
    uint16_t _d = (uint16_t)(dst); \
    for (uint8_t _i = 0; _i < _cnt; _i++) { \
        bool _old_x = g_m68k.flag_X; \
        g_m68k.flag_X = g_m68k.flag_C = (_d & 1) != 0; \
        _d = (_d >> 1) | (_old_x ? 0x8000u : 0u); \
    } \
    if (!((count) & 63)) g_m68k.flag_C = g_m68k.flag_X; \
    g_m68k.flag_V = false; \
    m68k_update_nz16(_d); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)(_d); \
} while(0)

#define M68K_ROXR32(dst, count) do { \
    uint8_t _cnt = (uint8_t)((count) & 63) % 33; \
    uint32_t _d = (uint32_t)(dst); \
    for (uint8_t _i = 0; _i < _cnt; _i++) { \
        bool _old_x = g_m68k.flag_X; \
        g_m68k.flag_X = g_m68k.flag_C = (_d & 1) != 0; \
        _d = (_d >> 1) | (_old_x ? 0x80000000u : 0u); \
    } \
    if (!((count) & 63)) g_m68k.flag_C = g_m68k.flag_X; \
    g_m68k.flag_V = false; \
    m68k_update_nz32(_d); \
    (dst) = _d; \
} while(0)

/* --- SWAP (swap halves of data register) --- */
#define M68K_SWAP(dst) do { \
    uint32_t _d = (uint32_t)(dst); \
    _d = (_d >> 16) | (_d << 16); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz32(_d); \
    (dst) = _d; \
} while(0)

/* --- EXT (sign extend) --- */
#define M68K_EXT16(dst) do { \
    int16_t _res = (int8_t)(uint8_t)(dst); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz16((uint16_t)_res); \
    (dst) = ((dst) & ~0xFFFFu) | (uint16_t)((uint16_t)_res); \
} while(0)

#define M68K_EXT32(dst) do { \
    int32_t _res = (int16_t)(uint16_t)(dst); \
    g_m68k.flag_C = false; g_m68k.flag_V = false; \
    m68k_update_nz32((uint32_t)_res); \
    (dst) = (uint32_t)_res; \
} while(0)

/* ================================================================
 * CPU lifecycle
 * ================================================================ */

/* Reset CPU to power-on state */
void recomp_m68k_reset(void);

/* Handle exceptions (interrupts, traps, etc.) */
void recomp_m68k_exception(uint8_t vector);

#endif /* GENRECOMP_M68K_H */
