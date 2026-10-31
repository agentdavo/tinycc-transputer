/*
 * tvx_ref.h - independent reference model for the TVX low-precision formats.
 *
 * Written directly from:
 *   - IEEE 754-2019 binary32 / bfloat16 round-to-nearest-even
 *   - OCP Microscaling Formats (MX) Specification v1.0 (E2M1 elements,
 *     E8M0 shared scale)
 *
 * Deliberately uses different algorithms from the emulator's implementation in
 * transputer/t4/tvx.h (real arithmetic and libm here, integer bit surgery and
 * tables there) so that agreement between the two is meaningful evidence.
 *
 * Used by tvx_conformance.c (exhaustive host cross-check) and by tvx_genvec.c
 * (which generates the expected-value tables baked into test_tvx.c).
 */

#ifndef TVX_REF_H
#define TVX_REF_H

#include <math.h>
#include <stdint.h>

static uint32_t ref_f2b(float f) { union { float f; uint32_t u; } v; v.f = f; return v.u; }
static float    ref_b2f(uint32_t u) { union { float f; uint32_t u; } v; v.u = u; return v.f; }

/* ================================================================== */
/* Reference model - written from the specifications, not from tvx.h   */
/* ================================================================== */

/* IEEE binary32 -> bfloat16, round-to-nearest-even, via explicit
 * round/sticky reasoning on the 16 discarded bits. */
static inline uint32_t ref_f32_to_bf16(uint32_t x)
{
    uint32_t hi  = (x >> 16) & 0xFFFFu;
    uint32_t low = x & 0xFFFFu;

    if ((x & 0x7F800000u) == 0x7F800000u) {
        if (x & 0x007FFFFFu)
            return hi | 0x0040u;       /* NaN -> quiet NaN, sign preserved */
        return hi;                     /* Inf passes through */
    }
    if (low > 0x8000u)
        hi++;                          /* above halfway: round up */
    else if (low == 0x8000u && (hi & 1u))
        hi++;                          /* exactly halfway: to even */
    return hi & 0xFFFFu;
}

static inline uint32_t ref_bf16_to_f32(uint32_t x)
{
    return (x & 0xFFFFu) << 16;
}

/* E2M1: 1 sign, 2 exponent (bias 1), 1 mantissa. Decoded arithmetically. */
static inline double ref_mxfp4_value(uint32_t code)
{
    int s = (code >> 3) & 1;
    int e = (code >> 1) & 3;
    int m = code & 1;
    double v;

    if (e == 0)
        v = ldexp((double)m, -1);                     /* subnormal: m * 2^-1 */
    else
        v = ldexp(1.0 + 0.5 * (double)m, e - 1);      /* normal            */
    return s ? -v : v;
}

static inline uint32_t ref_mxfp4_to_f32(uint32_t code)
{
    return ref_f2b((float)ref_mxfp4_value(code & 0xFu));
}

/* binary32 -> E2M1, round-to-nearest-even over the representable grid. */
static inline uint32_t ref_f32_to_mxfp4(uint32_t x)
{
    static const double grid[8] = { 0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0 };
    double a = fabs((double)ref_b2f(x));
    uint32_t sign = (x >> 31) ? 8u : 0u;
    uint32_t best;
    int i;

    if (isnan(a) || isinf(a))
        return sign | 7u;              /* E2M1 has no Inf/NaN: saturate */

    if (a >= grid[7])
        return sign | 7u;

    best = 0;
    for (i = 0; i < 7; i++) {
        double lo = grid[i], hi = grid[i + 1];
        if (a >= lo && a <= hi) {
            double dl = a - lo, dh = hi - a;
            if (dl < dh)        best = (uint32_t)i;
            else if (dh < dl)   best = (uint32_t)i + 1u;
            else                best = ((uint32_t)i & 1u) ? (uint32_t)i + 1u
                                                          : (uint32_t)i;
            break;
        }
    }
    return sign | best;
}

/* E8M0 -> binary32.  X = 2^(E-127); E=255 is NaN; there is no zero. */
static inline uint32_t ref_e8m0_to_f32(uint32_t e)
{
    e &= 0xFFu;
    if (e == 255u)
        return 0x7FC00000u;
    return ref_f2b((float)ldexp(1.0, (int)e - 127));
}

/* binary32 amax -> E8M0, per OCP MX v1.0:
 *     shared_exp = floor(log2(amax)) - emax_elem,  emax_elem(E2M1) = 2
 *     E          = clamp(shared_exp + 127, 0, 254) */
static inline uint32_t ref_e8m0_of(uint32_t x)
{
    double a = fabs((double)ref_b2f(x));
    int exp2, se;

    if (isnan(a) || isinf(a))
        return 255u;
    if (a == 0.0)
        return 0u;

    frexp(a, &exp2);               /* a = m * 2^exp2, m in [0.5, 1) */
    se = (exp2 - 1) - 2 + 127;     /* floor(log2 a) - emax_elem + bias */
    if (se < 0)   se = 0;
    if (se > 254) se = 254;
    return (uint32_t)se;
}

#endif /* TVX_REF_H */
