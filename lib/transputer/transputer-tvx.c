/* Software TVX, part 1: the MX and bfloat16 scalar, SIMD and dot
 * instructions, for every part without TVX.  transputer-gen.c calls these
 * where -mt9000tvx would emit MXFP4TOR32 .. BF16ADD2, MXFP4GEMVBF, Q8DOT and
 * Q8DOTBF; transputer-tvx-iq.c has the i-quant dots and transputer-tvx-qdot.c
 * QSETFMT/QDOT, each in its own object so a program pulls in only the
 * codebooks it uses.
 *
 * Every function repeats the T4 emulator's implementation of its
 * instruction (transputer/t4/p.c) on the same semantic headers, vendored
 * verbatim in tvx/, with the same floating-point operations in
 * the same order, so a program gets the same bits with or without TVX.
 * Memory is read the way the instruction reads it: blocks and bfloat16
 * vectors byte by byte, so they need no alignment; f32 activations as
 * words.
 *
 * Built into every transputer runtime (transputer-libtcc1*.a) with
 * -nostdinc against tvx/include, a libc-free <stdint.h> and
 * friends.  The float arithmetic is the part's own: the FPU on the T800,
 * T805 and T9000, lib/transputer/transputer-softfp.c on the T425 and T414. */
#include "tvx/tvx.h"

/* A little-endian halfword at p, as the instructions read bfloat16 and
 * binary16 values. */
static uint32_t tvx_half_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

uint32_t __tvx_bf16pack(uint32_t hi, uint32_t lo)
{
    return tvx_bf16_pack(hi, lo);
}

uint32_t __tvx_bf16unpkh(uint32_t x)
{
    return tvx_bf16_unpack_hi(x);
}

uint32_t __tvx_mxfp4tof32(uint32_t x)
{
    return tvx_mxfp4_to_f32(x);
}

uint32_t __tvx_f32tomxfp4(uint32_t x)
{
    return tvx_f32_to_mxfp4(x);
}

uint32_t __tvx_mxe8m0tof32(uint32_t x)
{
    return tvx_e8m0_to_f32(x);
}

uint32_t __tvx_mxe8m0of(uint32_t x)
{
    return tvx_e8m0_of(x);
}

/* MXFP4UNPK8: the 8 E2M1 codes packed in w, low nibble first, to 8 floats */
void __tvx_mxfp4unpk8(uint32_t w, uint32_t *dst)
{
    int i;
    for (i = 0; i < 8; i++)
        dst[i] = tvx_mxfp4_to_f32((w >> (4 * i)) & 0xFu);
}

/* R32TOMXFP4X8: 8 floats to 8 E2M1 codes, packed low nibble first */
uint32_t __tvx_f32tomxfp4x8(const uint32_t *src)
{
    uint32_t w = 0;
    int i;
    for (i = 0; i < 8; i++)
        w |= tvx_f32_to_mxfp4(src[i]) << (4 * i);
    return w;
}

uint32_t __tvx_bf16mul2(uint32_t a, uint32_t b)
{
    return tvx_bf16_pair_mul(a, b);
}

uint32_t __tvx_bf16add2(uint32_t a, uint32_t b)
{
    return tvx_bf16_pair_add(a, b);
}

/* MXFP4DOT: acc + scale * dot(block, act[0..31]) */
float __tvx_mxfp4dot(const uint8_t *blk, const uint32_t *act, float acc)
{
    float t = tvx_mxfp4_block_acc(blk + 1, act);
    float s = tvx_bits_to_float(tvx_e8m0_to_f32(blk[0]));
    return acc + (float)(s * t);
}

/* BF16DOT: acc + dot(x[0..n-1], y[0..n-1]) over bfloat16 vectors */
float __tvx_bf16dot(const uint8_t *x, const uint8_t *y, uint32_t n, float acc)
{
    float t = 0.0f;
    uint32_t i;
    for (i = 0; i < n; i++)
        t = t + (float)(
            tvx_bits_to_float(tvx_bf16_to_f32(tvx_half_at(x + 2 * i))) *
            tvx_bits_to_float(tvx_bf16_to_f32(tvx_half_at(y + 2 * i))));
    return acc + t;
}

/* MXFP4GEMV: nblk chained MXFP4DOTs */
float __tvx_mxfp4gemv(const uint8_t *blk, const uint32_t *act, uint32_t nblk,
                      float acc)
{
    uint32_t b;
    for (b = 0; b < nblk; b++) {
        const uint8_t *bp = blk + b * 17;
        float t = tvx_mxfp4_block_acc(bp + 1, act + b * 32);
        float s = tvx_bits_to_float(tvx_e8m0_to_f32(bp[0]));
        acc = acc + (float)(s * t);
    }
    return acc;
}

/* MXFP4GEMVBF: MXFP4GEMV against bfloat16 activations */
float __tvx_mxfp4gemvbf(const uint8_t *blk, const uint8_t *act, uint32_t nblk,
                        float acc)
{
    uint32_t a[32], b;
    int i;
    for (b = 0; b < nblk; b++) {
        const uint8_t *bp = blk + b * 17;
        const uint8_t *ap = act + b * 64;
        float t, s;
        for (i = 0; i < 32; i++)
            a[i] = tvx_bf16_to_f32(tvx_half_at(ap + 2 * i));
        t = tvx_mxfp4_block_acc(bp + 1, a);
        s = tvx_bits_to_float(tvx_e8m0_to_f32(bp[0]));
        acc = acc + (float)(s * t);
    }
    return acc;
}

/* Q8DOT and Q8DOTBF: acc + sum over q8_0 blocks of d * dot(qs, act) */
static float tvx_q8dot(const uint8_t *blk, const uint8_t *act, uint32_t nblk,
                       float acc, int is_bf)
{
    int8_t qs[32];
    uint32_t a[32], b;
    int i;
    for (b = 0; b < nblk; b++) {
        const uint8_t *bp = blk + b * 34;
        const uint8_t *ap = act + b * (is_bf ? 64 : 128);
        float t, d;
        for (i = 0; i < 32; i++)
            qs[i] = (int8_t)bp[2 + i];
        for (i = 0; i < 32; i++)
            a[i] = is_bf ? tvx_bf16_to_f32(tvx_half_at(ap + 2 * i))
                         : ((const uint32_t *)ap)[i];
        t = tvx_q8_block_acc(qs, a);
        d = tvx_bits_to_float(tvx_f16_to_f32(tvx_half_at(bp)));
        acc = acc + (float)(d * t);
    }
    return acc;
}

float __tvx_q8dot(const uint8_t *blk, const uint8_t *act, uint32_t nblk,
                  float acc)
{
    return tvx_q8dot(blk, act, nblk, acc, 0);
}

float __tvx_q8dotbf(const uint8_t *blk, const uint8_t *act, uint32_t nblk,
                    float acc)
{
    return tvx_q8dot(blk, act, nblk, acc, 1);
}
