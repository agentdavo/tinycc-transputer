/* Software TVX, part 2: IQ1MDOT, IQ2XXSDOT and IQ3XXSDOT, for every part
 * without TVX (see transputer-tvx.c).  They decode with TVX's own i-quant
 * decoders (tvx_vec.h), which apply the block scale differently from
 * llama.cpp's and so round differently from QDOT on the same format.
 *
 * tvx_vec.h also includes the GGUF decoders, which these do not use;
 * claiming their include guard keeps those decoders' codebooks out of this
 * object. */
#define TVX_GGUF_DECODE_H
#include "transputer-tvx/tvx_vec.h"

/* acc + sum over nblk blocks of dot(decode(block), act[256]) */
static float tvx_iqdot(const uint8_t *blk, const float *act, uint32_t nblk,
                       float acc, int fmt)
{
    uint32_t raw[TVX_IQ3XXS_BYTES / 4 + 1];   /* the largest of the three */
    float dec[256];
    uint32_t bsz = fmt == 0 ? TVX_IQ1M_BYTES
                 : fmt == 1 ? TVX_IQ2XXS_BYTES : TVX_IQ3XXS_BYTES;
    uint32_t b, i;

    for (b = 0; b < nblk; b++) {
        const uint8_t *bp = blk + b * bsz;
        const float *ap = act + b * 256;
        float t = 0.0f;

        for (i = 0; i < bsz; i++)
            ((uint8_t *)raw)[i] = bp[i];
        if (fmt == 0)      tvx_iq1m_decode((const uint8_t *)raw, dec);
        else if (fmt == 1) tvx_iq2xxs_decode((const uint8_t *)raw, dec);
        else               tvx_iq3xxs_decode((const uint8_t *)raw, dec);
        for (i = 0; i < 256; i++)
            t = t + (float)(dec[i] * ap[i]);
        acc = acc + t;
    }
    return acc;
}

float __tvx_iq1mdot(const uint8_t *blk, const float *act, uint32_t nblk,
                    float acc)
{
    return tvx_iqdot(blk, act, nblk, acc, 0);
}

float __tvx_iq2xxsdot(const uint8_t *blk, const float *act, uint32_t nblk,
                      float acc)
{
    return tvx_iqdot(blk, act, nblk, acc, 1);
}

float __tvx_iq3xxsdot(const uint8_t *blk, const float *act, uint32_t nblk,
                      float acc)
{
    return tvx_iqdot(blk, act, nblk, acc, 2);
}
