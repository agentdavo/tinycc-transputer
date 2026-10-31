/* One QGEMM of ROWS rows at the real input width KDIM, weights of ggml type
 * FMT (block NE elements, BSZ bytes), activations by ACT: 0 f32, 1 bf16,
 * 8 Q8_0 (VQ80), 15 Q8_K (VQ8K).  Quantised weights cycle the real blocks of
 * gguf_all_formats.inc; float weights are synthetic.  Prints "start",
 * "quant" and "gemv" so T4_T9K_MARKS=1 times each phase. */
#include "gguf/gguf_all_formats.inc"
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void us(const char *s) { while (*s) UART_DATA = (unsigned)(unsigned char)*s++; }
void __vsetlen(int n);
void __vsetfmt(unsigned fmt);
void __qsetfmt(int ggml_type);
int __qgemm(float *dst, const void *param, unsigned nblocks);
void __vq8k(void *dst, const void *src);
void __vq80(void *dst, const void *src);
void *memcpy(void *d, const void *s, unsigned n);

#define NB (KDIM / NE)
static unsigned char W[ROWS * NB * BSZ];
static float x[KDIM], out[ROWS];
static unsigned short xbf[KDIM];
static unsigned char xq[(KDIM / 32) * 34 + (KDIM / 256) * 292];
static struct { const void *acts; const void *weights; } prm;

int test_main(void)
{
    int i, k;

    for (k = 0; k < QALL_N && qall_fmt[k] != FMT; k++)
        ;
    for (i = 0; i < ROWS * NB; i++) {
        if (k < QALL_N)
            memcpy(W + i * BSZ, qall_blk[k] + (i % QALL_NB) * BSZ, BSZ);
        else
            W[i * BSZ] = (unsigned char)i;          /* float weights */
    }
    for (i = 0; i < KDIM; i++) {
        x[i] = (float)((i % 31) - 15) * 0.0078125f;
        xbf[i] = 0x3c00u;
    }
    __qsetfmt(FMT);
    us("start\n");
    prm.acts = x;
#if ACT == 15 || ACT == 8
    __vsetfmt(0);
    __vsetlen(KDIM);
# if ACT == 15
    __vq8k(xq, x);
# else
    __vq80(xq, x);
# endif
    prm.acts = xq;
    __vsetfmt(3);
#elif ACT == 1
    prm.acts = xbf;
    __vsetfmt(1);
#else
    __vsetfmt(0);
#endif
    us("quant\n");
    prm.weights = W;
    __vsetlen(ROWS);
    __qgemm(out, &prm, NB);
    us("gemv\n");
    __vsetfmt(0);
    return 0;
}
