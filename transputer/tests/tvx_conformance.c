/*
 * tvx_conformance.c - exhaustive host-side conformance check for the TVX
 * (Transputer Vector eXtension) low-precision instruction semantics.
 *
 * This links the *emulator's* semantic core (transputer/t4/tvx.h) against an
 * independently written reference model derived directly from:
 *
 *   - IEEE 754-2019 binary32 / bfloat16 round-to-nearest-even
 *   - OCP Microscaling Formats (MX) Specification v1.0, element type E2M1
 *     and shared scale type E8M0
 *
 * The two implementations deliberately use different algorithms: tvx.h uses
 * integer bit surgery and precomputed tables, the reference below uses
 * libm (frexp/ldexp) and real arithmetic over the representable grid.
 *
 * Scalar conversions are checked over their *entire* input domain:
 *   - MXFP4TOR32   : all 2^4  codes
 *   - MXE8M0TOR32  : all 2^8  codes
 *   - R32TOBF16    : all 2^32 binary32 inputs
 *   - R32TOMXFP4   : all 2^32 binary32 inputs
 *   - MXE8M0OF     : all 2^32 binary32 inputs
 *
 * Build:  cc -O2 -o tvx_conformance tvx_conformance.c -lm
 * Run  :  ./tvx_conformance          (full sweep)
 *         ./tvx_conformance --quick  (specials + a strided sweep)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "../t4/tvx.h"
#include "tvx_ref.h"

static long failures = 0;
static long checks   = 0;

static void fail(const char *what, uint32_t in, uint32_t got, uint32_t want)
{
    if (failures < 20)
        fprintf(stderr, "FAIL %-12s in=0x%08X got=0x%08X want=0x%08X\n",
                what, in, got, want);
    failures++;
}

#define f2b ref_f2b
#define b2f ref_b2f

/* ================================================================== */

static void check(const char *what, uint32_t in, uint32_t got, uint32_t want)
{
    checks++;
    if (got != want)
        fail(what, in, got, want);
}

/* NaN bit patterns differ only in payload for some paths; compare exactly. */
static void sweep_all(int quick)
{
    uint64_t i;
    uint64_t step = quick ? 1021u : 1u;   /* prime stride keeps coverage wide */
    uint64_t n = 0x100000000ull;

    for (i = 0; i < n; i += step) {
        uint32_t x = (uint32_t)i;
        check("f32->bf16",  x, tvx_f32_to_bf16(x),  ref_f32_to_bf16(x));
        check("f32->mxfp4", x, tvx_f32_to_mxfp4(x), ref_f32_to_mxfp4(x));
        check("e8m0of",     x, tvx_e8m0_of(x),      ref_e8m0_of(x));
    }
}

static void sweep_specials(void)
{
    static const uint32_t sp[] = {
        0x00000000u, 0x80000000u,                 /* +0, -0                */
        0x00000001u, 0x80000001u,                 /* min subnormal         */
        0x007FFFFFu, 0x00800000u,                 /* max subn, min normal  */
        0x00400000u,                              /* 2^-127 (E8M0 E=0)     */
        0x3E800000u, 0x3F400000u, 0x3FA00000u,    /* 0.25, 0.75, 1.25      */
        0x3FE00000u, 0x40200000u, 0x40600000u,    /* 1.75, 2.5,  3.5       */
        0x40A00000u,                              /* 5.0                   */
        0x3F000000u, 0x3F800000u, 0x3FC00000u,    /* 0.5, 1.0, 1.5         */
        0x40000000u, 0x40400000u, 0x40800000u,    /* 2.0, 3.0, 4.0         */
        0x40C00000u, 0x40C00001u,                 /* 6.0, just above 6     */
        0x7F7FFFFFu, 0xFF7FFFFFu,                 /* +-max finite          */
        0x7F800000u, 0xFF800000u,                 /* +-Inf                 */
        0x7F800001u, 0xFF800001u,                 /* signalling NaN        */
        0x7FC00000u, 0xFFC00000u,                 /* quiet NaN             */
        0x7FBFFFFFu, 0x7FFFFFFFu
    };
    size_t k;
    uint32_t sign;

    for (k = 0; k < sizeof(sp) / sizeof(sp[0]); k++) {
        uint32_t x = sp[k];
        check("f32->bf16",  x, tvx_f32_to_bf16(x),  ref_f32_to_bf16(x));
        check("f32->mxfp4", x, tvx_f32_to_mxfp4(x), ref_f32_to_mxfp4(x));
        check("e8m0of",     x, tvx_e8m0_of(x),      ref_e8m0_of(x));
    }

    /* also apply both signs to every special magnitude */
    for (k = 0; k < sizeof(sp) / sizeof(sp[0]); k++) {
        for (sign = 0; sign <= 1; sign++) {
            uint32_t x = (sp[k] & 0x7FFFFFFFu) | (sign << 31);
            check("f32->bf16",  x, tvx_f32_to_bf16(x),  ref_f32_to_bf16(x));
            check("f32->mxfp4", x, tvx_f32_to_mxfp4(x), ref_f32_to_mxfp4(x));
            check("e8m0of",     x, tvx_e8m0_of(x),      ref_e8m0_of(x));
        }
    }
}

static void sweep_small_domains(void)
{
    uint32_t c;

    for (c = 0; c < 16; c++)
        check("mxfp4->f32", c, tvx_mxfp4_to_f32(c), ref_mxfp4_to_f32(c));

    for (c = 0; c < 256; c++)
        check("e8m0->f32", c, tvx_e8m0_to_f32(c), ref_e8m0_to_f32(c));

    /* bf16 -> f32 is exact and total: check all 2^16 codes */
    for (c = 0; c < 65536; c++)
        check("bf16->f32", c, tvx_bf16_to_f32(c), ref_bf16_to_f32(c));
}

/* Round-trip law: every representable value must survive f32 -> small -> f32 */
static void check_roundtrips(void)
{
    uint32_t c;

    for (c = 0; c < 16; c++) {
        uint32_t f = tvx_mxfp4_to_f32(c);
        uint32_t back = tvx_f32_to_mxfp4(f);
        check("mxfp4 rt", c, back, c);
    }
    for (c = 0; c < 65536; c++) {
        uint32_t f = tvx_bf16_to_f32(c);
        uint32_t back = tvx_f32_to_bf16(f);
        /* NaN codes are canonicalised to quiet NaN; everything else is exact */
        if ((c & 0x7F80u) == 0x7F80u && (c & 0x007Fu) != 0)
            check("bf16 rt", c, back, c | 0x0040u);
        else
            check("bf16 rt", c, back, c);
    }
}

/* Quantisation law from the OCP spec: with the scale chosen by MXE8M0OF, the
 * block maximum normalises onto the top of the E2M1 range (4.0, i.e. exponent
 * aligned to emax_elem = 2) and so never overflows.
 *
 * The one exception is the bottom of the E8M0 range: the smallest encodable
 * scale is 2^-127, so for amax <= 2^-126 the ideal shared exponent is below
 * E=0 and is clamped.  There the normalised value lands *below* 4.0, which is
 * still in range - that is the whole point of clamping down rather than up. */
static void check_scale_selection(void)
{
    uint32_t i;

    for (i = 1; i < 255; i++) {
        /* amax = 2^(i-127) exactly */
        uint32_t amax = i << 23;
        uint32_t e    = tvx_e8m0_of(amax);
        float scale   = b2f(tvx_e8m0_to_f32(e));
        float norm    = b2f(amax) / scale;
        uint32_t code = tvx_f32_to_mxfp4(f2b(norm));
        int clamped   = ((int)i - 127 - 2 + 127) < 0;
        uint32_t want = clamped ? 4u : 6u;   /* 2.0 when clamped, else 4.0 */

        checks++;
        if (code != want) {
            if (failures < 20)
                fprintf(stderr,
                        "FAIL scale-sel  E=%u amax=2^%d -> norm=%g code=%u want=%u\n",
                        e, (int)i - 127, (double)norm, code, want);
            failures++;
        }
    }
}

/* tvx_mxfp4_block_acc must equal a straightforward sequential float loop. */
static void check_block_acc(void)
{
    uint8_t nib[16];
    uint32_t act[32];
    unsigned seed = 12345u;
    int trial, i;

    for (trial = 0; trial < 20000; trial++) {
        float ref = 0.0f, got;
        for (i = 0; i < 16; i++) {
            seed = seed * 1103515245u + 12345u;
            nib[i] = (uint8_t)(seed >> 16);
        }
        for (i = 0; i < 32; i++) {
            seed = seed * 1103515245u + 12345u;
            /* activations in a benign exponent range */
            act[i] = f2b(((float)(int)(seed >> 20) - 2048.0f) / 256.0f);
        }
        for (i = 0; i < 32; i++) {
            /* GGUF split-half order */
            uint32_t n = (i < 16) ? (uint32_t)(nib[i] & 0x0Fu)
                                  : (uint32_t)(nib[i - 16] >> 4);
            ref = ref + (float)(b2f(ref_mxfp4_to_f32(n)) * b2f(act[i]));
        }
        got = tvx_mxfp4_block_acc(nib, act);
        checks++;
        if (f2b(got) != f2b(ref)) {
            if (failures < 20)
                fprintf(stderr, "FAIL block-acc trial=%d got=0x%08X want=0x%08X\n",
                        trial, f2b(got), f2b(ref));
            failures++;
        }
    }
}

/* Block rotation.
 *
 * Two independent properties are checked, because bit-exactness alone would
 * not catch a transform that is exactly reproducible but not orthogonal:
 *
 *  1. tvx_had32_rotate must equal a naive O(n^2) multiply by the sign-flipped
 *    Hadamard matrix, whose entries come from the parity of popcount(i & j) -
 *    a completely different derivation from the butterfly network.  This is
 *    checked to a tolerance, since the two summation orders differ.
 *
 *  2. the transform must preserve the L2 norm (it is orthogonal), which is
 *    the whole reason the rotation can be folded into the weights offline.
 */
static int had32_sign(int i, int j)
{
    int p = i & j, c = 0;
    while (p) { c ^= p & 1; p >>= 1; }
    return c ? -1 : 1;
}

static void check_rotation(void)
{
    unsigned seed = 987654321u;
    int trial, i, j;

    for (trial = 0; trial < 5000; trial++) {
        float in[32], got[32], ref[32];
        uint32_t mask;
        double n_in = 0.0, n_out = 0.0;

        seed = seed * 1103515245u + 12345u;
        mask = (trial & 1) ? seed : 0u;
        for (i = 0; i < 32; i++) {
            seed = seed * 1103515245u + 12345u;
            in[i] = ((float)(int)(seed >> 20) - 2048.0f) / 256.0f;
        }

        /* reference: sign flip, then dense Hadamard, then 1/sqrt(32) */
        for (i = 0; i < 32; i++) {
            double acc = 0.0;
            for (j = 0; j < 32; j++) {
                double x = in[j];
                if ((mask >> j) & 1u) x = -x;
                acc += had32_sign(i, j) * x;
            }
            ref[i] = (float)(acc * 0.1767766952966369);
        }

        for (i = 0; i < 32; i++) got[i] = in[i];
        tvx_had32_rotate(got, mask);

        for (i = 0; i < 32; i++) {
            double d = (double)got[i] - (double)ref[i];
            if (d < 0) d = -d;
            checks++;
            if (d > 1e-3) {
                if (failures < 20)
                    fprintf(stderr, "FAIL had32 trial=%d i=%d got=%g want=%g\n",
                            trial, i, (double)got[i], (double)ref[i]);
                failures++;
            }
            n_in  += (double)in[i]  * (double)in[i];
            n_out += (double)got[i] * (double)got[i];
        }

        /* orthogonality: the L2 norm is unchanged */
        checks++;
        if (n_in > 0.0 && (n_out / n_in < 0.999 || n_out / n_in > 1.001)) {
            if (failures < 20)
                fprintf(stderr, "FAIL had32 norm trial=%d in=%g out=%g\n",
                        trial, n_in, n_out);
            failures++;
        }
    }
}

/* The MXQROT tail must agree with the OCP conversion rule applied by hand. */
static void check_quantize_block(void)
{
    unsigned seed = 24681357u;
    int trial, i;

    for (trial = 0; trial < 5000; trial++) {
        float v[32], amax = 0.0f, s;
        uint8_t got[17];
        uint32_t e;

        for (i = 0; i < 32; i++) {
            seed = seed * 1103515245u + 12345u;
            v[i] = ((float)(int)(seed >> 18) - 8192.0f) / 512.0f;
        }
        tvx_mxfp4_quantize_block(got, v);

        for (i = 0; i < 32; i++) {
            float a = v[i] < 0.0f ? -v[i] : v[i];
            if (a > amax) amax = a;
        }
        e = ref_e8m0_of(f2b(amax));
        s = b2f(ref_e8m0_to_f32(e));

        checks++;
        if (got[0] != (uint8_t)e) {
            if (failures < 20)
                fprintf(stderr, "FAIL mxqrot scale trial=%d got=%u want=%u\n",
                        trial, got[0], e);
            failures++;
        }
        for (i = 0; i < 32; i++) {
            uint32_t want = ref_f32_to_mxfp4(f2b((float)(v[i] / s)));
            uint32_t have = (i < 16) ? (uint32_t)(got[1 + i] & 0x0Fu)
                                     : (uint32_t)(got[1 + i - 16] >> 4);
            checks++;
            if (have != want) {
                if (failures < 20)
                    fprintf(stderr, "FAIL mxqrot elem trial=%d i=%d got=%u want=%u\n",
                            trial, i, have, want);
                failures++;
            }
        }
    }
}

int main(int argc, char **argv)
{
    int quick = (argc > 1 && strcmp(argv[1], "--quick") == 0);

    printf("TVX conformance: emulator core vs independent spec reference\n");
    printf("mode: %s\n", quick ? "quick (strided sweep)" : "full (all 2^32)");

    sweep_small_domains();
    printf("  small domains        ok (%ld checks)\n", checks);

    check_roundtrips();
    check_scale_selection();
    check_block_acc();
    printf("  round-trips / scale / block-acc  ok (%ld checks)\n", checks);

    check_rotation();
    check_quantize_block();
    printf("  block rotation / quantise        ok (%ld checks)\n", checks);

    sweep_specials();
    sweep_all(quick);

    printf("total checks: %ld   failures: %ld\n", checks, failures);
    if (failures) {
        printf("TVX CONFORMANCE: FAIL\n");
        return 1;
    }
    printf("TVX CONFORMANCE: PASS\n");
    return 0;
}
