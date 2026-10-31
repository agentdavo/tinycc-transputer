/*
 * tvx.h - user-facing header for the Transputer Vector eXtension:
 *         MXFP4 (OCP microscaling E2M1 + E8M0) and bfloat16.
 *
 * See transputer/TVX-ISA.md for the instruction specification.
 *
 * The scalar intrinsics take and return raw bit patterns, exactly as the
 * instructions do.  The inline wrappers below give typed float versions.
 *
 * The __bf16 type is built into the compiler; no declaration is needed.
 */

#ifndef _TRANSPUTER_TVX_H
#define _TRANSPUTER_TVX_H

/* -mt9000tvx (which predefines __TVX__) runs these as instructions.  On
 * every other part the MX/bfloat16 scalar and SIMD builtins and the
 * quantised dots (__mxfp4dot .. __qdot) compile to calls into the runtime's
 * software TVX, which gives the same bits; the vector kernels, block
 * rotation, __qgemm and TVX-GFX are compile errors there. */

/* ---- raw-bits intrinsics (map 1:1 onto instructions) ---------------- */
/*
 * unsigned __bf16tof32(unsigned bf16_bits);       BF16TOR32
 * unsigned __f32tobf16(unsigned f32_bits);        R32TOBF16
 * unsigned __bf16pack(unsigned hi, unsigned lo);  BF16PACK
 * unsigned __bf16unpkh(unsigned packed);          BF16UNPKH
 *
 * unsigned __mxfp4tof32(unsigned e2m1_code);      MXFP4TOR32
 * unsigned __f32tomxfp4(unsigned f32_bits);       R32TOMXFP4
 * unsigned __mxe8m0tof32(unsigned e8m0_byte);     MXE8M0TOR32
 * unsigned __mxe8m0of(unsigned amax_f32_bits);    MXE8M0OF
 *
 * float __mxfp4dot(const void *block, const float *act, float acc);
 * float __bf16dot(const void *x, const void *y, int n, float acc);
 */

/* An MXFP4 block: one E8M0 scale byte followed by 32 E2M1 nibbles.
 * Bit-identical to llama.cpp / GGUF `block_mxfp4`: element j is the low
 * nibble of q[j], element j+16 the high nibble of q[j] (split-half order). */
#define TVX_MXFP4_BLOCK_ELEMS  32
#define TVX_MXFP4_BLOCK_BYTES  17

typedef struct {
    unsigned char scale;     /* E8M0 */
    unsigned char q[16];     /* 32 x E2M1 */
} tvx_mxfp4_block;

/* ---- typed helpers -------------------------------------------------- */

static inline unsigned int tvx_f2b(float f)
{
    union { float f; unsigned int u; } v;
    v.f = f;
    return v.u;
}

static inline float tvx_b2f(unsigned int u)
{
    union { float f; unsigned int u; } v;
    v.u = u;
    return v.f;
}

/* bfloat16 <-> float */
static inline float          tvx_bf16_to_float(unsigned short b)
{ return tvx_b2f(__bf16tof32((unsigned int)b)); }

static inline unsigned short tvx_float_to_bf16(float f)
{ return (unsigned short)__f32tobf16(tvx_f2b(f)); }

/* MXFP4 element <-> float */
static inline float          tvx_mxfp4_to_float(unsigned int code)
{ return tvx_b2f(__mxfp4tof32(code)); }

static inline unsigned int   tvx_float_to_mxfp4(float f)
{ return __f32tomxfp4(tvx_f2b(f)); }

/* E8M0 shared scale <-> float */
static inline float          tvx_scale_to_float(unsigned char e)
{ return tvx_b2f(__mxe8m0tof32((unsigned int)e)); }

static inline unsigned char  tvx_scale_of(float amax)
{ return (unsigned char)__mxe8m0of(tvx_f2b(amax)); }

/* Quantise 32 floats into one MXFP4 block, per the OCP conversion rule:
 * pick the shared scale from the block maximum, then round each element.
 *
 * The element loop uses the 8-lane R32TOMXFP4X8 instruction: four
 * instructions instead of 32 scalar conversions plus 32 shift/mask/or
 * sequences.  Measured ~36x faster than the scalar form. */
static inline void tvx_quantize_block(tvx_mxfp4_block *out, const float *in)
{
    float tmp[32];
    float amax = 0.0f;
    float scale;
    unsigned int w;
    int i, k;

    for (i = 0; i < 32; i++) {
        float a = in[i] < 0.0f ? -in[i] : in[i];
        if (a > amax)
            amax = a;
    }
    out->scale = tvx_scale_of(amax);
    scale = tvx_scale_to_float(out->scale);

    for (i = 0; i < 32; i++)
        tmp[i] = in[i] / scale;

    /* Quantise 8 elements per instruction, then place them in GGUF
     * split-half order: elements 0..15 are low nibbles, 16..31 high. */
    for (k = 0; k < 2; k++) {
        unsigned int lo0 = __f32tomxfp4x8((unsigned int)(unsigned long)&tmp[k * 8]);
        unsigned int hi0 = __f32tomxfp4x8((unsigned int)(unsigned long)&tmp[16 + k * 8]);
        for (i = 0; i < 8; i++) {
            unsigned int l = (lo0 >> (4 * i)) & 0x0Fu;
            unsigned int h = (hi0 >> (4 * i)) & 0x0Fu;
            out->q[k * 8 + i] = (unsigned char)(l | (h << 4));
        }
    }
    (void)w;
}

/* Dequantise one block into 32 floats using the 8-lane unpack instruction:
 * four instructions instead of 32 scalar decodes. */
static inline void tvx_dequantize_block(float *out, const tvx_mxfp4_block *b)
{
    float scale = tvx_scale_to_float(b->scale);
    int k, i;

    /* GGUF split-half: low nibbles -> elements 0..15, high -> 16..31 */
    for (k = 0; k < 2; k++) {
        unsigned int lo = 0, hi = 0;
        for (i = 0; i < 8; i++) {
            unsigned int byte = b->q[k * 8 + i];
            lo |= (byte & 0x0Fu) << (4 * i);
            hi |= (byte >> 4)    << (4 * i);
        }
        __mxfp4unpk8(lo, (unsigned int)(unsigned long)&out[k * 8]);
        __mxfp4unpk8(hi, (unsigned int)(unsigned long)&out[16 + k * 8]);
    }
    for (i = 0; i < 32; i++)
        out[i] = out[i] * scale;
}

/* ---- block rotation (BRQ) ------------------------------------------- */
/*
 * void __vsetlen(int n_elements);
 * void __vhad32  (float *dst,           const float *src, unsigned mask);
 * void __mxqrot  (tvx_mxfp4_block *dst, const float *src, unsigned mask);
 * void __mxqrotbf(tvx_mxfp4_block *dst, const unsigned short *src, unsigned mask);
 *
 * The rotation is the normalised 32-point Hadamard, applied independently
 * within each block, optionally preceded by a per-lane sign flip (`mask`
 * bit i set => negate lane i; pass 0 for the plain transform).  This is the
 * BRQ recipe from arXiv:2511.04214: a *global* rotation of the kind QuaRot
 * and SpinQuant use makes MXFP4 worse, because it inflates the scale of the
 * many regular blocks to relieve the few outlier ones, and MXFP4's
 * power-of-two block scale reconstructs large values poorly.
 *
 * Only the activation side needs these.  On the weight side the rotation is
 * orthogonal and is folded into W offline, so MXFP4DOT / MXFP4GEMV / QDOT
 * are unchanged and consume ordinary `block_mxfp4` data.
 *
 * The sign mask must be the same one that was folded into the weights.
 */

/* Rotate and quantise `n` activations (a multiple of 32) into n/32 blocks.
 * This is the whole activation-side path in one instruction per block:
 * without it, the 160 butterflies per block would cost more than the GEMV
 * they feed. */
static inline void tvx_rotate_quantize(tvx_mxfp4_block *out, const float *in,
                                       int n, unsigned int mask)
{
    __vsetlen(n);
    __mxqrot(out, in, mask);
}

/* Same, from bfloat16 activations - pairs with __mxfp4gemvbf's bf16 path. */
static inline void tvx_rotate_quantize_bf16(tvx_mxfp4_block *out,
                                            const unsigned short *in,
                                            int n, unsigned int mask)
{
    __vsetlen(n);
    __mxqrotbf(out, in, mask);
}

/* Rotation on its own, float in / float out.  Needed for the down-projection
 * input (QuaRot's R4), where BRQ replaces an O(N^2) online rotation with an
 * O(N*32) one. */
static inline void tvx_rotate(float *out, const float *in,
                              int n, unsigned int mask)
{
    __vsetlen(n);
    __vhad32(out, in, mask);
}

/* ---- quantised activations: the integer dot ------------------------- */
/*
 * void __vq8k(void *dst, const void *src);
 * void __vq80(void *dst, const void *src);
 *
 * Quantise __vsetlen() activations - f32, bf16 or f16 by __vsetfmt(0/1/2) -
 * into llama.cpp block_q8_K (per 256 elements an f32 scale, 256 int8 and 16
 * int16 group sums, 292 bytes) or block_q8_0 (per 32 an f16 scale and 32
 * int8, 34 bytes).  After __vsetfmt(3), __qgemm takes those blocks as its
 * activations and runs ggml's generic integer dot for the __qsetfmt weight
 * format instead of the f32 one: exact integer sums, then the block scales.
 *
 * Each weight format pairs with the activation format llama.cpp gives it:
 *   Q8_K (__vq8k): q2_K..q6_K, iq1_s, iq1_m, iq2_xxs, iq2_xs, iq2_s,
 *                  iq3_xxs, iq3_s, iq4_xs
 *   Q8_0 (__vq80): q4_0, q5_0, q8_0, iq4_nl, mxfp4
 * Anything else (q4_1 and q5_1, whose partner is Q8_1, and float weights)
 * is refused: __qgemm returns 0 and writes nothing.
 *
 * Quantise once per activation vector; every row of every __qgemm on that
 * vector reuses the blocks.
 *
 * The activation buffer also keeps a vector that fits (16 KB on the tvx3
 * machine) after the __qgemm that read it, tagged with its address.  Give
 * the next __qgemm TVX_KEEP_ACTS(acts) as its activation pointer when the
 * program has not written that vector since: if the buffer still holds it,
 * the read is skipped - gate and up of one input, or the row chunks of one
 * matrix, load it once.  Bit 0 of the pointer carries the promise, and only
 * the timing depends on it: the result is the same either way.
 */
#define TVX_Q8K_BLOCK_BYTES 292
#define TVX_Q80_BLOCK_BYTES 34
#define TVX_KEEP_ACTS(p)    ((const void *)((unsigned)(p) | 1u))

/* __qdot and __qgemm (f32 or bf16 activations) also take unquantised
 * weights - __qsetfmt(0) f32, (1) f16, (30) bf16 - in blocks of 32 elements,
 * so a router or a small float projection is one __qgemm like the rest. */

/* acc[0..n-1] += w * y[0..n-1] - the weighted sum of expert outputs - as
 * three vector ops; tmp is n floats of scratch.  About 1.5 cycles per
 * element on the T9000 model, against ~20 for the plain C loop. */
static inline void tvx_scaled_accumulate(float *acc, const float *y, float w,
                                         float *tmp, int n)
{
    __vfill(tmp, tvx_f2b(w), (unsigned int)n * 4u);
    __vsetlen(n);
    __vmul(tmp, y, tmp);
    __vadd(acc, acc, tmp);
}

/* acc + dot over n consecutive blocks - one instruction for the whole row. */
static inline float tvx_gemv(const tvx_mxfp4_block *blocks,
                             const float *act, int n, float acc)
{
    return __mxfp4gemv(blocks, act, n, acc);
}

/* Packed two-lane bfloat16 arithmetic (2 lanes per 32-bit word). */
static inline unsigned int tvx_bf16x2_mul(unsigned int a, unsigned int b)
{ return __bf16mul2(a, b); }

static inline unsigned int tvx_bf16x2_add(unsigned int a, unsigned int b)
{ return __bf16add2(a, b); }

/* acc + scale * dot(block, act[0..31]) */
static inline float tvx_dot_block(const tvx_mxfp4_block *b,
                                  const float *act, float acc)
{
    return __mxfp4dot(b, act, acc);
}

/* acc + dot(x[0..n-1], y[0..n-1]) over bfloat16 vectors */
static inline float tvx_dot_bf16(const unsigned short *x,
                                 const unsigned short *y, int n, float acc)
{
    return __bf16dot(x, y, n, acc);
}

#endif /* _TRANSPUTER_TVX_H */
