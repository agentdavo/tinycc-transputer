/*
 * tvx_vec.h - TVX vector kernels for transformer inference.
 *
 * These implement the memory-to-memory vector instructions (OPR 0x1CF..0x1D5)
 * in the MOVE / MOVE2D tradition: one dispatch does a whole vector, because
 * profiling showed that on the T4 the per-element interpreter dispatch, not
 * the arithmetic, is what costs.  See transputer/TVX-ISA.md section 8.
 *
 * Numerical contract
 * ------------------
 * Every kernel accumulates and evaluates transcendentals in *double*, and
 * rounds to binary32 exactly once, when storing each result.  That makes the
 * results reproducible on any IEEE-754 host and lets the on-target tests
 * compare against expected values generated from the same definition.
 *
 * Included only by the emulator (p.c), never by the compiler, so that libm
 * does not leak into the TCC build.
 */

#ifndef TVX_VEC_H
#define TVX_VEC_H

#include <math.h>
#include <stdint.h>

#include "tvx.h"
#include "tvx_iq.h"
#include "tvx_gguf_decode.h"

/* ------------------------------------------------------------------ */
/* GGUF i-quants: IQ1_M, IQ2_XXS, IQ3_XXS                              */
/* ------------------------------------------------------------------ */
/*
 * These are grid-codebook formats: a small index selects 8 packed int8
 * values from a shared table.  The decoders below are transcriptions of
 * llama.cpp's dequantize_row_* so the T4 decodes them byte-identically.
 * QK_K is 256, so each block covers 256 elements.
 *
 *   block_iq1_m   : qs[32], qh[16], scales[8]                = 56 bytes
 *   block_iq2_xxs : binary16 d, uint16 qs[32]                = 66 bytes
 *   block_iq3_xxs : binary16 d, uint8 qs[96]                 = 98 bytes
 *
 * Each writes 256 decoded binary32 values into `out`.
 */
#define TVX_QK_K       256
#define TVX_IQ1M_BYTES 56
#define TVX_IQ2XXS_BYTES 66
#define TVX_IQ3XXS_BYTES 98
#define TVX_IQ1S_DELTA 0.125f

static inline void tvx_iq1m_decode(const uint8_t *blk, float *out)
{
    const uint8_t *qs = blk;            /* QK_K/8  = 32 */
    const uint8_t *qh = blk + 32;       /* QK_K/16 = 16 */
    const uint8_t *scb = blk + 48;      /* QK_K/32 =  8 */
    uint16_t sc[4];
    uint32_t su;
    float d, delta[4];
    uint16_t idx[4];
    int ib, l, j;

    for (l = 0; l < 4; l++)
        sc[l] = (uint16_t)(scb[2*l] | (scb[2*l+1] << 8));
    su = (uint32_t)((sc[0] >> 12) | ((sc[1] >> 8) & 0x00f0)
                  | ((sc[2] >> 4) & 0x0f00) | (sc[3] & 0xf000));
    d = tvx_bits_to_float (tvx_f16_to_f32 (su));

    for (ib = 0; ib < TVX_QK_K/32; ++ib) {
        float dl1 = d * (float)(2*((sc[ib/2] >> (6*(ib%2)+0)) & 0x7) + 1);
        float dl2 = d * (float)(2*((sc[ib/2] >> (6*(ib%2)+3)) & 0x7) + 1);

        idx[0] = (uint16_t)(qs[0] | ((qh[0] << 8) & 0x700));
        idx[1] = (uint16_t)(qs[1] | ((qh[0] << 4) & 0x700));
        idx[2] = (uint16_t)(qs[2] | ((qh[1] << 8) & 0x700));
        idx[3] = (uint16_t)(qs[3] | ((qh[1] << 4) & 0x700));
        delta[0] = (qh[0] & 0x08) ? -TVX_IQ1S_DELTA : TVX_IQ1S_DELTA;
        delta[1] = (qh[0] & 0x80) ? -TVX_IQ1S_DELTA : TVX_IQ1S_DELTA;
        delta[2] = (qh[1] & 0x08) ? -TVX_IQ1S_DELTA : TVX_IQ1S_DELTA;
        delta[3] = (qh[1] & 0x80) ? -TVX_IQ1S_DELTA : TVX_IQ1S_DELTA;

        for (l = 0; l < 2; ++l) {
            const int8_t *g = (const int8_t *)(tvx_iq1s_grid + idx[l]);
            for (j = 0; j < 8; ++j) out[j] = dl1 * ((float)g[j] + delta[l]);
            out += 8;
        }
        for (l = 2; l < 4; ++l) {
            const int8_t *g = (const int8_t *)(tvx_iq1s_grid + idx[l]);
            for (j = 0; j < 8; ++j) out[j] = dl2 * ((float)g[j] + delta[l]);
            out += 8;
        }
        qs += 4; qh += 2;
    }
}

static inline void tvx_iq2xxs_decode(const uint8_t *blk, float *out)
{
    float d = tvx_bits_to_float (tvx_f16_to_f32 (
                  (uint32_t)blk[0] | ((uint32_t)blk[1] << 8)));
    const uint8_t *qs = blk + 2;   /* 32 uint16, little-endian */
    int ib32, l, j;

    for (ib32 = 0; ib32 < TVX_QK_K/32; ++ib32) {
        const uint8_t *p = qs + 8*ib32;
        uint32_t a0 = (uint32_t)p[0] | ((uint32_t)p[1]<<8)
                    | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
        uint32_t a1 = (uint32_t)p[4] | ((uint32_t)p[5]<<8)
                    | ((uint32_t)p[6]<<16) | ((uint32_t)p[7]<<24);
        const uint8_t *aux8 = (const uint8_t *)&a0;
        float db = d * (0.5f + (float)(a1 >> 28)) * 0.25f;
        uint8_t tmp[4];
        tmp[0]=(uint8_t)(a0&0xFF); tmp[1]=(uint8_t)((a0>>8)&0xFF);
        tmp[2]=(uint8_t)((a0>>16)&0xFF); tmp[3]=(uint8_t)((a0>>24)&0xFF);
        aux8 = tmp;
        for (l = 0; l < 4; ++l) {
            const uint8_t *g = (const uint8_t *)(tvx_iq2xxs_grid + aux8[l]);
            uint8_t signs = tvx_ksigns_iq2xs[(a1 >> 7*l) & 127];
            for (j = 0; j < 8; ++j)
                out[j] = db * (float)g[j] * ((signs & tvx_kmask_iq2xs[j]) ? -1.f : 1.f);
            out += 8;
        }
    }
}

static inline void tvx_iq3xxs_decode(const uint8_t *blk, float *out)
{
    float d = tvx_bits_to_float (tvx_f16_to_f32 (
                  (uint32_t)blk[0] | ((uint32_t)blk[1] << 8)));
    const uint8_t *qs = blk + 2;
    const uint8_t *ss = qs + TVX_QK_K/4;   /* scales_and_signs */
    int ib32, l, j;

    for (ib32 = 0; ib32 < TVX_QK_K/32; ++ib32) {
        const uint8_t *p = ss + 4*ib32;
        uint32_t a = (uint32_t)p[0] | ((uint32_t)p[1]<<8)
                   | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
        float db = d * (0.5f + (float)(a >> 28)) * 0.5f;
        for (l = 0; l < 4; ++l) {
            uint8_t signs = tvx_ksigns_iq2xs[(a >> 7*l) & 127];
            const uint8_t *g1 = (const uint8_t *)(tvx_iq3xxs_grid + qs[2*l+0]);
            const uint8_t *g2 = (const uint8_t *)(tvx_iq3xxs_grid + qs[2*l+1]);
            for (j = 0; j < 4; ++j) {
                out[j+0] = db * (float)g1[j] * ((signs & tvx_kmask_iq2xs[j+0]) ? -1.f : 1.f);
                out[j+4] = db * (float)g2[j] * ((signs & tvx_kmask_iq2xs[j+4]) ? -1.f : 1.f);
            }
            out += 8;
        }
        qs += 8;
    }
}

/* SiLU / swish: x * sigmoid(x) = x / (1 + exp(-x)) */
static inline double tvx_silu_exact(double x)
{
    return x / (1.0 + exp(-x));
}

/* RMSNorm: out_i = x_i * g_i / sqrt(mean(x^2) + eps).
 * eps is fixed by the instruction at 1e-6, the value used by essentially
 * every published transformer implementation. */
#define TVX_RMSNORM_EPS 1e-6

static inline double tvx_rmsnorm_scale(const double *ss, uint32_t n)
{
    return 1.0 / sqrt(*ss / (double)n + TVX_RMSNORM_EPS);
}

#endif /* TVX_VEC_H */
