/* Software TVX, part 3: QSETFMT and QDOT, the generic GGUF dot, for every
 * part without TVX (see transputer-tvx.c).  Blocks are decoded with
 * llama.cpp's own dequantize_row_* (tvx_gguf_decode.h), so this object
 * carries every GGUF codebook.
 *
 * The format is a ggml type id held in a latch, as QSETFMT holds it in the
 * processor: one per program here, shared by all its processes.  Besides the
 * GGUF block formats QDOT takes F32 (0), F16 (1) and BF16 (30) weights in
 * blocks of 32. */
#include "tvx/tvx_gguf_decode.h"

static int tvx_quant_fmt;

void __tvx_qsetfmt(int fmt)
{
    tvx_quant_fmt = fmt;
}

static int tvx_qfmt_block_bytes(int fmt)
{
    switch (fmt) {
    case 0:  return 32 * 4;
    case 1:
    case 30: return 32 * 2;
    default: return tvx_gguf_block_bytes(fmt);
    }
}

static int tvx_qfmt_decode_block(int fmt, const uint8_t *raw, float *out)
{
    int i;

    switch (fmt) {
    case 0:
        for (i = 0; i < 32; i++)
            out[i] = tvx_bits_to_float((uint32_t)raw[4*i]
                     | (uint32_t)raw[4*i + 1] << 8
                     | (uint32_t)raw[4*i + 2] << 16
                     | (uint32_t)raw[4*i + 3] << 24);
        return 32;
    case 1:
    case 30:
        for (i = 0; i < 32; i++) {
            uint32_t h = (uint32_t)raw[2*i] | (uint32_t)raw[2*i + 1] << 8;
            out[i] = tvx_bits_to_float(fmt == 1 ? tvx_f16_to_f32(h)
                                                : tvx_bf16_to_f32(h));
        }
        return 32;
    default:
        return tvx_gguf_decode_block(fmt, raw, out);
    }
}

/* acc + sum over nblk blocks of the QSETFMT format of dot(decode, act) */
float __tvx_qdot(const uint8_t *blk, const float *act, uint32_t nblk,
                 float acc)
{
    uint32_t raw[64];           /* a block, word aligned for the decoders */
    float dec[256];
    int fmt = tvx_quant_fmt;
    int bsz = tvx_qfmt_block_bytes(fmt);
    uint32_t b;
    int i, n;

    if (bsz <= 0 || bsz > (int)sizeof raw)
        return acc;
    for (b = 0; b < nblk; b++) {
        const uint8_t *bp = blk + b * (uint32_t)bsz;
        const float *ap;
        float t = 0.0f;

        for (i = 0; i < bsz; i++)
            ((uint8_t *)raw)[i] = bp[i];
        n = tvx_qfmt_decode_block(fmt, (const uint8_t *)raw, dec);
        if (n <= 0)
            break;
        ap = act + b * (uint32_t)n;
        for (i = 0; i < n; i++)
            t = t + (float)(dec[i] * ap[i]);
        acc = acc + t;
    }
    return acc;
}
