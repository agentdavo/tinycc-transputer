/*
 * Target: -mt9000tvx
 * test_tvx_qgemm_keep.c - QGEMM's keep-activations bit.
 * Expected: SQKEEP-OK\nH
 *
 * Bit 0 of QGEMM's activation pointer (TVX_KEEP_ACTS in include/tvx.h)
 * promises the vector is unchanged since a QGEMM loaded it, so the
 * activation buffer need not read it again.  Only the cycle model looks at
 * it: results must be bit-identical with and without it, on the integer
 * (Q8_K) and the f32 path, for q5_K and the ternary iq1_s - and also when
 * the promise is broken (another vector, or the same address rewritten),
 * because the emulator always reads memory.
 */
#include "gguf/gguf_all_formats.inc"
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}

void __vsetlen(int n);
void __vsetfmt(unsigned fmt);
void __qsetfmt(int ggml_type);
int __qgemm(float *dst, const void *param, unsigned nblocks);
void __vq8k(void *dst, const void *src);
void *memcpy(void *d, const void *s, unsigned n);

#ifndef TVX_KEEP_ACTS
#define TVX_KEEP_ACTS(p)    ((const void *)((unsigned)(p) | 1u))
#endif

#define NB   QALL_NB                    /* blocks per row */
#define NE   (256 * NB)
#define ROWS 3
static float x[NE], y[NE];
static unsigned char xq[292 * NB], yq[292 * NB];
static unsigned char w[ROWS * NB * 176];
static float o1[ROWS], o2[ROWS];
static struct { const void *acts; const void *weights; } prm;
static int bad;

static void gemm(float *out, const void *acts, int qfmt)
{
    prm.acts = acts;
    prm.weights = w;
    __qsetfmt(qfmt);
    __vsetfmt(3 * (qfmt != 0));
    __vsetlen(ROWS);
    __qgemm(out, &prm, NB);
}

static void same(const char *what)
{
    int r;
    for (r = 0; r < ROWS; r++)
        if (f2b(o1[r]) != f2b(o2[r])) {
            us(what); us(" differs\n");
            bad++;
            return;
        }
}

int test_main(void)
{
    static const int fmt[2] = { 13, 19 }, bsz[2] = { 176, 50 };
    int f, k, r, b, i;

    for (i = 0; i < NE; i++) {
        x[i] = (float)((i % 29) - 14) * 0.03125f;
        y[i] = (float)((i % 17) - 8) * 0.0625f;
    }
    __vsetfmt(0);
    __vsetlen(NE);
    __vq8k(xq, x);
    __vq8k(yq, y);

    for (f = 0; f < 2; f++) {
        for (k = 0; k < QALL_N && qall_fmt[k] != fmt[f]; k++)
            ;
        for (r = 0; r < ROWS; r++)
            for (b = 0; b < NB; b++)
                memcpy(w + (r * NB + b) * bsz[f],
                       qall_blk[k] + ((r + 2 * b) % QALL_NB) * bsz[f], bsz[f]);

        /* 1: the promise kept - gate then up of one input */
        gemm(o1, xq, fmt[f]);
        gemm(o2, TVX_KEEP_ACTS(xq), fmt[f]);
        same("kept");
        /* 2: another vector under the bit: no tag match */
        gemm(o1, yq, fmt[f]);
        gemm(o2, xq, fmt[f]);                   /* the buffer holds xq */
        gemm(o2, TVX_KEEP_ACTS(yq), fmt[f]);
        same("other");
        /* 3: the promise broken - xq rewritten in place */
        gemm(o2, xq, fmt[f]);
        memcpy(xq, yq, sizeof xq);
        gemm(o2, TVX_KEEP_ACTS(xq), fmt[f]);
        same("rewritten");
        __vsetfmt(0);
        __vsetlen(NE);
        __vq8k(xq, x);                          /* back for the next format */
    }

    /* 4: the f32-activation path (q5_K weights against f32 x) */
    for (k = 0; k < QALL_N && qall_fmt[k] != 13; k++)
        ;
    for (r = 0; r < ROWS; r++)
        for (b = 0; b < NB; b++)
            memcpy(w + (r * NB + b) * 176, qall_blk[k] + ((r + b) % QALL_NB) * 176, 176);
    prm.weights = w;
    __qsetfmt(13);
    __vsetfmt(0);
    __vsetlen(ROWS);
    prm.acts = x;
    __qgemm(o1, &prm, NB);
    prm.acts = TVX_KEEP_ACTS(x);
    __qgemm(o2, &prm, NB);
    same("f32");

    us(bad ? "QKEEP-BAD\n" : "QKEEP-OK\n");
    return bad;
}
