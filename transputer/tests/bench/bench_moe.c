/* One DeepSeek-V4.1-Flash routed expert for one token, at the real sizes:
 *   x[5120] -> gate, up [2304 x 5120] -> SwiGLU -> down [5120 x 2304]
 *           -> acc += router_weight * y
 * Weights are format FMT (block bytes BSZ), filled by cycling the real
 * blocks of gguf_all_formats.inc.  Q8K=1 runs the Q8_K activation path,
 * Q8K=0 the f32 one.  Every phase prints a line, so T4_T9K_MARKS=1 gives a
 * cycle snapshot per phase. */
#include "gguf/gguf_all_formats.inc"
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void us(const char *s) { while (*s) UART_DATA = (unsigned)(unsigned char)*s++; }
void __vsetlen(int n);
void __vsetfmt(unsigned fmt);
void __qsetfmt(int ggml_type);
int __qgemm(float *dst, const void *param, unsigned nblocks);
void __vq8k(void *dst, const void *src);
void __vsilu(float *dst, const float *src);
void __vmul(float *dst, const float *a, const float *b);
void __vadd(float *dst, const float *a, const float *b);
void __vfill(void *dst, unsigned pattern, unsigned nbytes);
static unsigned f2b(float f) { union { float f; unsigned u; } v; v.f = f; return v.u; }

#define D   5120
#define F   2304
#define NBD (D / 256)
#define NBF (F / 256)
static unsigned char Wg[F * NBD * BSZ], Wu[F * NBD * BSZ], Wd[D * NBF * BSZ];
static float x[D], g[F], u[F], h[F], y[D], acc[D], wv[D];
static unsigned char xq[NBD * 292], hq[NBF * 292];
static struct { const void *acts; const void *weights; } prm;

void *memcpy(void *d, const void *s, unsigned n);
static void fill(unsigned char *w, int nblocks, int k)
{
    int b;
    for (b = 0; b < nblocks; b++)       /* inlined to the block move */
        memcpy(w + b * BSZ, qall_blk[k] + (b % QALL_NB) * BSZ, BSZ);
}

int test_main(void)
{
    int i, k;
    float rw = 0.37f;

    for (k = 0; k < QALL_N && qall_fmt[k] != FMT; k++)
        ;
    fill(Wg, F * NBD, k);
    fill(Wu, F * NBD, k);
    fill(Wd, D * NBF, k);
    for (i = 0; i < D; i++) {
        x[i] = (float)((i % 31) - 15) * 0.0078125f;
        wv[i] = rw;
    }
    __qsetfmt(FMT);
    us("start\n");

#if Q8K
    __vsetfmt(0); __vsetlen(D); __vq8k(xq, x);
    us("vq8k-x\n");
    prm.acts = xq;
    __vsetfmt(3);
#else
    prm.acts = x;
    __vsetfmt(0);
#endif
    __vsetlen(F);
    prm.weights = Wg; __qgemm(g, &prm, NBD);
    us("gate\n");
    prm.weights = Wu; __qgemm(u, &prm, NBD);
    us("up\n");
    __vsetlen(F); __vsilu(g, g); __vmul(h, g, u);
    us("swiglu\n");
#if Q8K
    __vsetfmt(0); __vsetlen(F); __vq8k(hq, h);
    us("vq8k-h\n");
    prm.acts = hq;
    __vsetfmt(3);
#else
    prm.acts = h;
#endif
    __vsetlen(D);
    prm.weights = Wd; __qgemm(y, &prm, NBF);
    us("down\n");
#if TVX_AXPY
    /* each expert has its own router weight, so the splat is per expert */
    __vfill(wv, f2b(rw), D * 4);
    __vsetlen(D); __vmul(y, y, wv); __vadd(acc, acc, y);
#else
    for (i = 0; i < D; i++)
        acc[i] += rw * y[i];
#endif
    us("accumulate\n");
    __vsetfmt(0);
    return 0;
}
