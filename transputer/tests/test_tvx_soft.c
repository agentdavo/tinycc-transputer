/*
 * Expected: Sconv=66527c65\nsimd=8c5e94e6\nbf16x2=2bc22552\nmxfp4=4fa282a6\nbf16dot=407fd43d\nq8=9bfbf76c\niq=4072f7c4\nqdot=e519b3f1\nTVXSOFT-DONE\nH
 *
 * test_tvx_soft.c - software TVX against the TVX instructions.  Every
 * builtin software TVX covers (the MX/bfloat16 scalar and SIMD builtins and
 * the quantised dots) runs over a few thousand inputs, and each group's
 * results go into one hash.  The test has no Target line, so it runs on
 * every part: at -mt9000tvx t4 executes the instructions, everywhere else
 * the calls go to lib/transputer-tvx*.c, and the expected hashes (taken
 * from the -mt9000tvx run) must match on all of them.
 *
 * The inputs are real GGUF blocks (Qwen3.8-27B and the DeepSeek/Qwen zoo)
 * and LCG data.  The conversions see every bit pattern, NaNs included; the
 * float arithmetic sees only finite inputs that cannot make a NaN, because
 * a NaN's payload may legitimately differ between the soft-float helpers
 * and the host FPU t4 runs on.
 *
 *   TP_EXTRA_CFLAGS=-mt414 T4_CPU=-s414 \
 *     bash transputer/tests/test_runner_with_uart.sh test_tvx_soft
 */
#include "gguf/qwen38_blocks.inc"
#include "gguf/gguf_all_formats.inc"

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void uh(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int n = (v >> i) & 0xF;
        up(n < 10 ? '0' + n : 'a' + n - 10);
    }
}
static unsigned f2b(float f) { union { float f; unsigned u; } v; v.f = f; return v.u; }
static float b2f(unsigned u) { union { float f; unsigned u; } v; v.u = u; return v.f; }

static unsigned rng = 0x9E3779B9u;
static unsigned rnd(void) { return rng = rng * 1664525u + 1013904223u; }
/* A float in [-2, 2) with a full 24-bit mantissa (exact conversion). */
static float rndf(void) { return (float)((int)(rnd() >> 8) - 8388608) * (1.0f / 4194304.0f); }
/* bfloat16 of such a float: finite, |x| < 2. */
static unsigned rndbf(void) { return f2b(rndf()) >> 16; }

static unsigned h;
static void mix(unsigned w) { h = (h ^ w) * 16777619u; }
static void report(const char *name)
{
    us(name); up('='); uh(h); up('\n');
    h = 2166136261u;
}

static const unsigned specials[] = {
    0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x00800000u,
    0x3F800000u, 0xBF800000u, 0x40C00000u, 0x40E00000u, 0x41000000u,
    0x3E800000u, 0x3E000000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u,
    0xFF800000u, 0x7FC00000u, 0xFFC00001u, 0x7F800001u, 0x3FC00000u,
    0x40400000u, 0x40200000u, 0x3F400000u, 0x40A00000u, 0x3FE00000u,
};
#define NSPECIALS (int)(sizeof specials / sizeof specials[0])

static float act[256 * 4];
static unsigned char actbf[256 * 4 * 2];
static unsigned char mx[32 * 17];
static unsigned char q80[16 * 34];
static unsigned wf[32 * 3];
static unsigned char wh[64 * 3];

static void fill_act(void)
{
    int i;
    for (i = 0; i < 256 * 4; i++) {
        unsigned b;
        act[i] = rndf();
        b = rndbf();
        actbf[2 * i] = (unsigned char)b;
        actbf[2 * i + 1] = (unsigned char)(b >> 8);
    }
}

/* A finite binary16 scale, 2^-8 .. 2^7 */
static unsigned rndhalf(void)
{
    return ((rnd() >> 16) & 0x83FFu) | ((7u + (rnd() >> 28)) << 10);
}

int test_main(void)
{
    unsigned out[8];
    float acc;
    int i, j, k, n;

    h = 2166136261u;

    /* ---- scalar conversions: integer only, every bit pattern ---- */
    for (i = 0; i < 32; i++)
        mix(__mxfp4tof32((unsigned)i));
    mix(__mxfp4tof32(0xFFFFFFF7u));
    for (i = 0; i < 261; i++)
        mix(__mxe8m0tof32((unsigned)i));
    mix(__mxe8m0tof32(0xFFFFFF7Fu));
    for (i = 0; i < NSPECIALS + 2000; i++) {
        unsigned v = i < NSPECIALS ? specials[i] : rnd();
        mix(__f32tomxfp4(v));
        mix(__mxe8m0of(v));
        mix(__bf16unpkh(v));
        mix(__bf16pack(v, rnd()));
    }
    report("conv");

    /* ---- 8-lane SIMD ---- */
    for (i = 0; i < 200; i++) {
        unsigned src[8];
        __mxfp4unpk8(rnd(), out);
        for (j = 0; j < 8; j++)
            mix(out[j]);
        for (j = 0; j < 8; j++)
            src[j] = i < 100 ? rnd() : f2b(rndf() * (float)(1 << (i % 6)));
        mix(__f32tomxfp4x8(src));
    }
    report("simd");

    /* ---- packed bfloat16 pairs: finite lanes ---- */
    for (i = 0; i < 1000; i++) {
        unsigned a = rndbf() << 16 | rndbf(), b = rndbf() << 16 | rndbf();
        if (i % 3 == 0)                 /* exponent 254: products overflow */
            a = (a & 0x807F807Fu) | 0x7F007F00u;
        mix(__bf16mul2(a, b));
        mix(__bf16add2(a, b));
    }
    report("bf16x2");

    /* ---- MXFP4: real blocks, then random ones with moderate scales ---- */
    fill_act();
    acc = 0.0f;
    for (k = 0; k < QALL_N; k++) {
        if (qall_fmt[k] != 39)
            continue;
        for (i = 0; i < QALL_NB; i++) {
            acc = __mxfp4dot(qall_blk[k] + i * 17, act + i * 32, acc);
            mix(f2b(acc));
        }
        mix(f2b(__mxfp4gemv(qall_blk[k], act, QALL_NB, 0.5f)));
        mix(f2b(__mxfp4gemvbf(qall_blk[k], actbf, QALL_NB, -0.5f)));
    }
    for (i = 0; i < 32 * 17; i++)
        mx[i] = (unsigned char)(rnd() >> 24);
    for (i = 0; i < 32; i++)
        mx[i * 17] = (unsigned char)(117 + (rnd() >> 28));
    for (n = 0; n <= 8; n++) {
        for (j = 0; j + n <= 32; j += 5) {
            mix(f2b(__mxfp4gemv(mx + j * 17, act, n, 0.0f)));
            mix(f2b(__mxfp4gemvbf(mx + j * 17, actbf, n, 1.0f)));
            if (n == 1)
                mix(f2b(__mxfp4dot(mx + j * 17, act + 32, 0.125f)));
        }
    }
    report("mxfp4");

    /* ---- bfloat16 dot ---- */
    for (n = 0; n <= 300; n += 7) {
        mix(f2b(__bf16dot(actbf, actbf + 600, n, 0.5f)));
        mix(f2b(__bf16dot(actbf + 2, actbf + 9, n, 0.0f)));   /* unaligned */
    }
    report("bf16dot");

    /* ---- Q8_0: real blocks, then random ones ---- */
    for (k = 0; k < QW_N; k++) {
        if (qw_fmt[k] != 8)
            continue;
        for (n = 1; n <= QW_NB; n++) {
            mix(f2b(__q8dot(qw_blk[k], act, n, 0.0f)));
            mix(f2b(__q8dotbf(qw_blk[k], actbf, n, 0.25f)));
        }
    }
    for (i = 0; i < 16 * 34; i++)
        q80[i] = (unsigned char)(rnd() >> 24);
    for (i = 0; i < 16; i++) {
        unsigned d = rndhalf();
        q80[i * 34] = (unsigned char)d;
        q80[i * 34 + 1] = (unsigned char)(d >> 8);
    }
    for (n = 1; n <= 16; n += 3) {
        mix(f2b(__q8dot(q80, act, n, -1.0f)));
        mix(f2b(__q8dotbf(q80 + 34, actbf, n > 15 ? 15 : n, 2.0f)));
    }
    report("q8");

    /* ---- i-quants: real IQ1_M, IQ2_XXS, IQ3_XXS blocks ---- */
    for (k = 0; k < QW_N + QALL_N; k++) {
        int fmt = k < QW_N ? qw_fmt[k] : qall_fmt[k - QW_N];
        const unsigned char *b = k < QW_N ? qw_blk[k] : qall_blk[k - QW_N];
        int nb = k < QW_N ? QW_NB : QALL_NB;
        for (n = 1; n <= nb; n++) {
            if (fmt == 29)      acc = __iq1mdot(b, act, n, 0.0f);
            else if (fmt == 16) acc = __iq2xxsdot(b, act, n, 1.0f);
            else if (fmt == 18) acc = __iq3xxsdot(b, act, n, -0.75f);
            else                break;
            mix(f2b(acc));
        }
    }
    report("iq");

    /* ---- QSETFMT + QDOT: float weights, an unknown format, and every
     *      Qwen3.8 entry from a nonzero accumulator ---- */
    for (i = 0; i < 32 * 3; i++) {
        unsigned b = f2b(rndf());
        wf[i] = b;
        wh[2 * i] = (unsigned char)(b >> 16);   /* as bf16 */
        wh[2 * i + 1] = (unsigned char)(b >> 24);
    }
    for (n = 1; n <= 3; n++) {
        __qsetfmt(0);
        mix(f2b(__qdot(wf, act, n, 0.0f)));
        __qsetfmt(30);
        mix(f2b(__qdot(wh, act, n, 0.0f)));
        __qsetfmt(1);                       /* the same bytes as binary16 */
        mix(f2b(__qdot(wh, act, n, 0.0f)));
    }
    __qsetfmt(99);
    mix(f2b(__qdot(wf, act, 3, 0.375f)));
    for (k = 0; k < QW_N; k++) {
        __qsetfmt(qw_fmt[k]);
        mix(f2b(__qdot(qw_blk[k], act, QW_NB, 0.25f)));
    }
    report("qdot");

    (void)b2f;
    us("TVXSOFT-DONE\n");
    return 0;
}
