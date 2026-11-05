/*
 * tvx.h - Transputer Vector eXtension (TVX) semantic core.
 *
 * Reference semantics for the low-precision (MXFP4 / bfloat16) instruction
 * group, OPR 0x1C0..0x1C9.  Written as pure header-only functions with no
 * emulator dependencies so that the same code can be linked into:
 *
 *   - the T4 emulator (transputer/t4/p.c)
 *   - the host conformance validator (transputer/tests/tvx_conformance.c)
 *
 * Formats
 * -------
 * bfloat16   : 1 sign / 8 exponent / 7 mantissa.  Bit-identical to the top
 *              16 bits of an IEEE-754 binary32.
 *
 * MXFP4      : OCP Microscaling Formats (MX) v1.0, element type E2M1.
 *              An *element* is 4 bits (a nibble), NOT a byte.
 *              1 sign / 2 exponent (bias 1) / 1 mantissa.
 *              Magnitudes: 0, 0.5, 1, 1.5, 2, 3, 4, 6.  No Inf, no NaN.
 *
 * E8M0       : the MX shared scale.  8 bits, pure exponent, bias 127.
 *              X = 2^(E-127) for E in [0,254];  E=255 encodes NaN.
 *              There is no encoding for zero.
 *
 * MXFP4 block (as addressed by MXFP4DOT), 17 bytes.  This is bit-identical
 * to llama.cpp / GGUF `block_mxfp4`, so GGUF expert tensors are consumed in
 * place with no repacking:
 *
 *      offset 0        : E8M0 shared scale byte
 *      offset 1 .. 16  : 16 data bytes, 32 E2M1 elements, *split-half* order
 *                        element j      -> bits [3:0] of byte at offset 1+j
 *                        element j + 16 -> bits [7:4] of byte at offset 1+j
 *
 * (GGUF stores the same values with a table of 2*E2M1 and a half-scale of
 * 2^(e-128); that is mathematically identical to E2M1 * 2^(e-127) here.  The
 * only difference is that GGUF maps code 8 to +0 where E2M1 defines -0.)
 */

#ifndef TVX_H
#define TVX_H

#include <stdint.h>

typedef union { float f; uint32_t u; } tvx_f32_t;

static inline float tvx_bits_to_float(uint32_t b)
{
    tvx_f32_t v; v.u = b; return v.f;
}

static inline uint32_t tvx_float_to_bits(float f)
{
    tvx_f32_t v; v.f = f; return v.u;
}


/* ------------------------------------------------------------------ */
/* bfloat16                                                            */
/* ------------------------------------------------------------------ */

/* BF16TOR32: widen a bfloat16 (low 16 bits of x) to binary32 bits. Exact. */
static inline uint32_t tvx_bf16_to_f32(uint32_t x)
{
    return (x & 0xFFFFu) << 16;
}

/* R32TOBF16: narrow binary32 bits to bfloat16, round-to-nearest-even.
 *
 * NaN is special-cased: the plain round-and-truncate path would turn a
 * signalling NaN such as 0x7F800001 into +Inf.  NaN is instead quieted and
 * forced to have a non-zero payload. */
static inline uint32_t tvx_f32_to_bf16(uint32_t x)
{
    uint32_t lsb, bias;

    if ((x & 0x7F800000u) == 0x7F800000u && (x & 0x007FFFFFu) != 0)
        return ((x >> 16) & 0xFFFFu) | 0x0040u;  /* quiet NaN, sign kept */

    lsb  = (x >> 16) & 1u;
    bias = 0x7FFFu + lsb;
    return ((x + bias) >> 16) & 0xFFFFu;
}

/* BF16PACK: two binary32 values -> one word holding two bfloat16.
 * lo goes in bits [15:0], hi in bits [31:16]. */
static inline uint32_t tvx_bf16_pack(uint32_t hi_f32, uint32_t lo_f32)
{
    return (tvx_f32_to_bf16(hi_f32) << 16) | tvx_f32_to_bf16(lo_f32);
}

/* BF16UNPKH: widen the high-half bfloat16 of a packed word to binary32. */
static inline uint32_t tvx_bf16_unpack_hi(uint32_t x)
{
    return x & 0xFFFF0000u;
}

/* Packed two-lane bfloat16 arithmetic.  Each 32-bit word holds two lanes:
 * lane 0 in bits [15:0], lane 1 in bits [31:16].  Each lane is widened to
 * binary32, the operation is performed in binary32, and the result is
 * rounded back to bfloat16 (round-to-nearest-even) - i.e. exactly what the
 * scalar path does, done twice per instruction. */
static inline uint32_t tvx_bf16_pair_mul(uint32_t a, uint32_t b)
{
    tvx_f32_t l0, l1, r0, r1;
    l0.u = tvx_bf16_to_f32(a);       r0.u = tvx_bf16_to_f32(b);
    l1.u = tvx_bf16_unpack_hi(a);    r1.u = tvx_bf16_unpack_hi(b);
    l0.f = l0.f * r0.f;
    l1.f = l1.f * r1.f;
    return (tvx_f32_to_bf16(l1.u) << 16) | tvx_f32_to_bf16(l0.u);
}

static inline uint32_t tvx_bf16_pair_add(uint32_t a, uint32_t b)
{
    tvx_f32_t l0, l1, r0, r1;
    l0.u = tvx_bf16_to_f32(a);       r0.u = tvx_bf16_to_f32(b);
    l1.u = tvx_bf16_unpack_hi(a);    r1.u = tvx_bf16_unpack_hi(b);
    l0.f = l0.f + r0.f;
    l1.f = l1.f + r1.f;
    return (tvx_f32_to_bf16(l1.u) << 16) | tvx_f32_to_bf16(l0.u);
}

/* ------------------------------------------------------------------ */
/* IEEE-754 binary16 (half)                                            */
/* ------------------------------------------------------------------ */
/*
 * Not the same thing as bfloat16: 5 exponent bits and 10 mantissa bits.
 * This is `ggml_half`, the scale type of every GGUF quantisation block, so
 * it is needed to consume Q8_0 and the i-quants.  It is also the format C23
 * calls _Float16.
 */

/* F16TOR32: widen binary16 (low 16 bits of x) to binary32.  Always exact. */
static inline uint32_t tvx_f16_to_f32(uint32_t x)
{
    uint32_t sign = (x & 0x8000u) << 16;
    uint32_t e    = (x >> 10) & 0x1Fu;
    uint32_t m    = x & 0x03FFu;

    if (e == 0) {
        if (m == 0)
            return sign;                         /* +-0 */
        /* subnormal: renormalise into a binary32 normal */
        e = 1;
        while (!(m & 0x0400u)) { m <<= 1; e--; }
        m &= 0x03FFu;
        return sign | ((e + 112u) << 23) | (m << 13);
    }
    if (e == 31)
        return sign | 0x7F800000u | (m << 13);   /* Inf / NaN */
    return sign | ((e + 112u) << 23) | (m << 13);
}

/* R32TOF16: narrow binary32 to binary16, round-to-nearest-even, with
 * overflow to Inf and gradual underflow to subnormals. */
static inline uint32_t tvx_f32_to_f16(uint32_t x)
{
    uint32_t sign = (x >> 16) & 0x8000u;
    int32_t  e    = (int32_t)((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t m    = x & 0x007FFFFFu;

    if (((x >> 23) & 0xFFu) == 0xFFu) {          /* Inf / NaN */
        if (m)
            return sign | 0x7E00u | (m >> 13);   /* quiet NaN, payload kept */
        return sign | 0x7C00u;
    }
    if (e >= 0x1F)
        return sign | 0x7C00u;                   /* overflow -> Inf */
    if (e <= 0) {
        uint32_t r;
        if (e < -10)
            return sign;                         /* underflow to zero */
        m |= 0x00800000u;                        /* restore implicit bit */
        r = m >> (uint32_t)(14 - e);
        /* round to nearest even on the discarded bits */
        if ((m >> (uint32_t)(13 - e)) & 1u) {
            uint32_t rest = m & ((1u << (uint32_t)(13 - e)) - 1u);
            if (rest || (r & 1u))
                r++;
        }
        return sign | r;
    }
    {
        uint32_t r = ((uint32_t)e << 10) | (m >> 13);
        if ((m >> 12) & 1u) {
            if ((m & 0x0FFFu) || (r & 1u))
                r++;                             /* may carry into exponent */
        }
        return sign | r;
    }
}

/* ------------------------------------------------------------------ */
/* Q8_0 (GGUF): 32 int8 quants with one binary16 scale, 34 bytes       */
/* ------------------------------------------------------------------ */
/*
 *      offset 0..1  : binary16 delta (little-endian)
 *      offset 2..33 : 32 signed 8-bit quants
 *
 * Value of element j is qs[j] * delta.  This is bit-identical to
 * llama.cpp `block_q8_0`.
 *
 * The inner accumulation mirrors MXFP4: products summed sequentially in
 * binary32, delta applied once at the end of the block.
 */
static inline float tvx_q8_block_acc(const int8_t *qs, const uint32_t *act)
{
    float t = 0.0f;
    int i;
    for (i = 0; i < 32; i++) {
        tvx_f32_t a;
        a.u = act[i];
        t = t + (float)((float)qs[i] * a.f);
    }
    return t;
}

/* ------------------------------------------------------------------ */
/* MXFP4 (E2M1) elements                                               */
/* ------------------------------------------------------------------ */

/* MXFP4TOR32: decode the E2M1 nibble in x[3:0] to binary32 bits. Exact. */
static inline uint32_t tvx_mxfp4_to_f32(uint32_t x)
{
    /* magnitudes 0, 0.5, 1, 1.5, 2, 3, 4, 6 as binary32 bit patterns */
    static const uint32_t mag[8] = {
        0x00000000u, /* 0.0 */
        0x3F000000u, /* 0.5 */
        0x3F800000u, /* 1.0 */
        0x3FC00000u, /* 1.5 */
        0x40000000u, /* 2.0 */
        0x40400000u, /* 3.0 */
        0x40800000u, /* 4.0 */
        0x40C00000u  /* 6.0 */
    };
    return mag[x & 7u] | ((x & 8u) ? 0x80000000u : 0u);
}

/* R32TOMXFP4: encode binary32 bits to an E2M1 nibble.
 *
 * Rounding is round-to-nearest, ties-to-even over the representable grid.
 * Magnitudes above 6 saturate to 6.  E2M1 has no Inf and no NaN, so Inf and
 * NaN inputs saturate to 6 with the input sign preserved (the OCP spec leaves
 * this to the implementation; this matches the microxcaling reference's
 * clamping behaviour).  The sign of zero is preserved (-0.0 -> 0b1000). */
static inline uint32_t tvx_f32_to_mxfp4(uint32_t x)
{
    /* midpoints between adjacent grid magnitudes, as binary32 bit patterns:
     * 0.25 0.75 1.25 1.75 2.5 3.5 5.0 */
    static const uint32_t mid[7] = {
        0x3E800000u, 0x3F400000u, 0x3FA00000u,
        0x3FE00000u, 0x40200000u, 0x40600000u, 0x40A00000u
    };
    uint32_t sign = (x >> 31) << 3;
    uint32_t a    = x & 0x7FFFFFFFu;   /* |x| bits; ordered as integers */
    uint32_t code = 0;
    int i;

    if (a >= 0x7F800000u)              /* Inf or NaN -> saturate */
        return sign | 7u;

    for (i = 0; i < 7; i++) {
        if (a > mid[i]) {
            code = (uint32_t)i + 1u;
        } else if (a == mid[i]) {
            /* exact tie: pick the even code */
            code = (i & 1) ? (uint32_t)i + 1u : (uint32_t)i;
            break;
        } else {
            break;
        }
    }
    return sign | code;
}

/* ------------------------------------------------------------------ */
/* E8M0 shared scale                                                   */
/* ------------------------------------------------------------------ */

/* MXE8M0TOR32: decode an E8M0 scale byte to binary32 bits.
 * E=255 -> NaN;  E in [1,254] -> 2^(E-127) (a normal binary32);
 * E=0 -> 2^-127, which is a binary32 subnormal. */
static inline uint32_t tvx_e8m0_to_f32(uint32_t x)
{
    uint32_t e = x & 0xFFu;
    if (e == 255u)
        return 0x7FC00000u;            /* NaN */
    if (e == 0u)
        return 0x00400000u;            /* 2^-127, subnormal */
    return e << 23;                    /* 2^(e-127) */
}

/* MXE8M0OF: given the binary32 bits of a block's maximum absolute element,
 * produce the E8M0 scale byte per the OCP MX v1.0 conversion rule:
 *
 *     shared_exp = floor(log2(amax)) - emax_elem      (emax_elem = 2 for E2M1)
 *     E          = clamp(shared_exp + 127, 0, 254)
 *
 * amax == 0 yields E = 0;  amax NaN or Inf yields E = 255 (NaN scale, which
 * per the spec poisons the whole block). */
static inline uint32_t tvx_e8m0_of(uint32_t x)
{
    uint32_t a = x & 0x7FFFFFFFu;
    uint32_t f;
    int e, se;

    if (a >= 0x7F800000u)
        return 255u;                   /* Inf / NaN */
    if (a == 0u)
        return 0u;

    if (a >= 0x00800000u) {
        e = (int)(a >> 23) - 127;      /* normal */
    } else {
        int h = 22;                    /* subnormal: MSB index of mantissa */
        f = a & 0x007FFFFFu;
        while (h > 0 && !(f & (1u << h)))
            h--;
        e = h - 149;
    }

    se = e - 2;                        /* emax_elem for E2M1 is 2 */
    se += 127;
    if (se < 0)   se = 0;
    if (se > 254) se = 254;
    return (uint32_t)se;
}

/* ------------------------------------------------------------------ */
/* Block dot products                                                  */
/* ------------------------------------------------------------------ */

/*
 * Both dot products are defined with a strictly sequential binary32
 * accumulation so that the result is bit-exactly reproducible by a plain
 * C `float` loop:
 *
 *   MXFP4DOT:  t = 0f;  for i in 0..31: t = fl32(t + fl32(e_i * act_i))
 *              FAreg   = fl32(FAreg + fl32(scale * t))
 *
 *   BF16DOT:   t = 0f;  for i in 0..n-1: t = fl32(t + fl32(x_i * y_i))
 *              FAreg   = fl32(FAreg + t)
 *
 * The scale is applied once, after the inner accumulation.
 */

/* ------------------------------------------------------------------ */
/* Block rotation (BRQ)                                                */
/* ------------------------------------------------------------------ */
/*
 * "Block Rotation is All You Need for MXFP4 Quantization" (arXiv:2511.04214)
 * shows that a *global* orthogonal rotation - the QuaRot/SpinQuant recipe
 * that works so well for INT4 - actively harms MXFP4, because rotation
 * redistributes outlier energy into the previously-small blocks and MXFP4's
 * per-block power-of-two scale reconstructs large values badly.  Rotating
 * *within* each 32-element block instead confines that amplification to the
 * block it came from.  The paper measures the best rotation dimension as
 * exactly the MX block size, which is what makes it expressible as a single
 * fixed-width instruction rather than as another GEMV.
 *
 * The transform is the normalised 32-point Walsh-Hadamard transform,
 * optionally preceded by a diagonal sign flip (the "randomised Hadamard"
 * of QuaRot/BRQ), i.e. R = H32 * diag(s) / sqrt(32).  R is orthogonal, so
 * folding R into the weights offline and applying R to the activations
 * online leaves the dot product unchanged.
 *
 * Definition, fixed here so that a plain C float loop reproduces the
 * instruction bit-for-bit, exactly as the dot products are:
 *
 *   1. v[i] = -src[i] if bit i of the sign mask is set, else src[i].
 *      (Negation is exact, so this step never rounds.)
 *   2. five in-place decimation-in-time butterfly stages, stride 1,2,4,8,16;
 *      each butterfly is one binary32 add and one binary32 subtract.
 *   3. v[i] = v[i] * 2^-2.5.
 *
 * The normalisation constant is 1/sqrt(32) == sqrt(2)/8.  Scaling by 2^-3 is
 * exact, so this is the correctly-rounded binary32 value and multiplying by
 * it is a single rounding.
 */

#define TVX_HAD32_NORM_BITS 0x3E3504F3u   /* sqrt(2)/8 == 1/sqrt(32) */

/* VHAD32 kernel: rotate 32 binary32 values in place. */
static inline void tvx_had32_rotate(float *v, uint32_t mask)
{
    float norm = tvx_bits_to_float(TVX_HAD32_NORM_BITS);
    int s, i, j;

    for (i = 0; i < 32; i++)
        if ((mask >> i) & 1u)
            v[i] = -v[i];

    for (s = 1; s < 32; s <<= 1)
        for (i = 0; i < 32; i += s << 1)
            for (j = i; j < i + s; j++) {
                float a = v[j], b = v[j + s];
                v[j]     = (float)(a + b);
                v[j + s] = (float)(a - b);
            }

    for (i = 0; i < 32; i++)
        v[i] = (float)(v[i] * norm);
}

/* Quantise 32 binary32 values into one 17-byte MXFP4 block, per the OCP
 * conversion rule: the shared scale comes from the block maximum, then each
 * element is divided by it and rounded to the E2M1 grid.  Elements are laid
 * out in GGUF split-half order - element j is the low nibble of q[j],
 * element j+16 the high nibble - so the result is `block_mxfp4`.
 *
 * This is the tail of MXQROT, and is also exactly what the C helper
 * tvx_quantize_block() in transputer/include/tvx.h computes. */
static inline void tvx_mxfp4_quantize_block(uint8_t *out, const float *v)
{
    float amax = 0.0f, s;
    uint32_t e;
    int i;

    for (i = 0; i < 32; i++) {
        float a = v[i] < 0.0f ? -v[i] : v[i];
        if (a > amax)
            amax = a;
    }
    e = tvx_e8m0_of(tvx_float_to_bits(amax));
    s = tvx_bits_to_float(tvx_e8m0_to_f32(e));

    out[0] = (uint8_t)e;
    for (i = 0; i < 16; i++) {
        uint32_t lo = tvx_f32_to_mxfp4(tvx_float_to_bits((float)(v[i] / s)));
        uint32_t hi = tvx_f32_to_mxfp4(tvx_float_to_bits((float)(v[i + 16] / s)));
        out[1 + i] = (uint8_t)(lo | (hi << 4));
    }
}

/* Inner accumulation of one 32-element MXFP4 block against 32 binary32
 * activations, excluding the shared scale. */
static inline float tvx_mxfp4_block_acc(const uint8_t *nibbles, const uint32_t *act)
{
    float t = 0.0f;
    int i;
    for (i = 0; i < 32; i++) {
        /* GGUF split-half order: low nibbles are elements 0..15,
           high nibbles are elements 16..31. */
        uint32_t n = (i < 16) ? (uint32_t)(nibbles[i] & 0x0Fu)
                              : (uint32_t)(nibbles[i - 16] >> 4);
        float e = tvx_bits_to_float(tvx_mxfp4_to_f32(n));
        t = t + (float)(e * tvx_bits_to_float(act[i]));
    }
    return t;
}

#endif /* TVX_H */
