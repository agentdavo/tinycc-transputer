/*
 * Target: -mt9000tvx
 * test_tvx_q8k.c - the integer-activation path of TVX.
 * Expected: SQ8K-OK\nH
 *
 * VQ8K and VQ80 quantise activations to llama.cpp block_q8_K and block_q8_0,
 * and QGEMM with __vsetfmt(3) runs llama.cpp's generic integer dot of the
 * weights against the activation format llama.cpp pairs them with (all
 * vendored verbatim in transputer/t4/tvx_q8k.h).  Weights are the real
 * blocks of gguf/gguf_all_formats.inc; expected values come from
 * gguf/q8k_ref.c, which checks every integer kernel against the vendored
 * dequantisers before it writes them.
 *
 * Checks:
 *   1. VQ8K of f32 activations is byte-identical to quantize_row_q8_K_ref;
 *   2. VQ8K of bf16 activations likewise;
 *   3. VQ80 of f32 activations is byte-identical to quantize_row_q8_0_ref;
 *   4. integer QGEMM is bit-identical to the reference for every format that
 *      has an integer kernel, on every row of a 3-row QGEMM;
 *   5. it differs from the f32-activation QDOT of the same blocks by no more
 *      than the activation quantisation error allows;
 *   6. a format with no integer kernel (q4_1, whose partner is Q8_1) writes
 *      no rows and returns 0.
 *
 * Regenerate the oracle: see gguf/q8k_ref.c.
 */
#include "gguf/gguf_all_formats.inc"
#include "gguf/gguf_q8k_dot.inc"
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}

void __vsetlen(int n);
void __vsetfmt(unsigned fmt);
void __qsetfmt(int ggml_type);
int __qgemm(float *dst, const void *param, unsigned nblocks);
float __qdot(const void *blocks, const float *acts, int nblocks, float acc);
void __vq8k(void *dst, const void *src);
void __vq80(void *dst, const void *src);
unsigned __f32tobf16(unsigned f32_bits);

#define NE   (256 * QALL_NB)
#define ROWS 3
static float a[NE];
static unsigned short vbf[NE];
static unsigned char q8[292 * (NE / 256)], q8b[292 * (NE / 256)];
static unsigned char q80[34 * (NE / 32)];
static unsigned char w[ROWS * QALL_NB * 256];
static float out[ROWS];
static struct { const void *acts; const void *weights; } prm;

static unsigned fnv(const unsigned char *p, int n)
{
    unsigned h = 2166136261u;
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

int test_main(void)
{
    int k, i, r, bad = 0;
    unsigned h;

    /* 1 and 3. f32 activations, the same ones test_tvx_allformats uses */
    for (i = 0; i < NE; i++)
        a[i] = (float)((i % 31) - 15) * 0.0078125f;
    __vsetfmt(0);
    __vsetlen(NE);
    __vq8k(q8, a);
    h = fnv(q8, sizeof q8);
    if (h != Q8K_ACT_HASH) { bad++; us("vq8k f32 "); uh(h); us("\n"); }
    __vq80(q80, a);
    h = fnv(q80, sizeof q80);
    if (h != Q80_ACT_HASH) { bad++; us("vq80 f32 "); uh(h); us("\n"); }

    /* 2. bf16 activations that are not exact in bfloat16 */
    for (i = 0; i < NE; i++)
        vbf[i] = (unsigned short)__f32tobf16(
                     f2b((float)((i * 37) % 201 - 100) * 0.013f));
    __vsetfmt(1);
    __vq8k(q8b, vbf);
    h = fnv(q8b, sizeof q8b);
    if (h != Q8K_ACT_HASH_BF) { bad++; us("vq8k bf16 "); uh(h); us("\n"); }

    /* 4 and 5: every format with an integer kernel */
    prm.weights = w;
    for (k = 0; k < QALL_N; k++) {
        int n = QALL_NB * q8k_bsz[k];
        float f, d;

        if (!q8k_act[k])
            continue;
        for (r = 0; r < ROWS; r++)
            for (i = 0; i < n; i++)
                w[r * n + i] = qall_blk[k][i];
        prm.acts = q8k_act[k] == 15 ? (const void *)q8 : (const void *)q80;
        __qsetfmt(qall_fmt[k]);
        __vsetfmt(3);
        __vsetlen(ROWS);
        r = __qgemm(out, &prm, QALL_NB);
        if (r != ROWS) { bad++; us(qall_name[k]); us(" rows\n"); }
        for (r = 0; r < ROWS; r++)
            if (f2b(out[r]) != q8k_dot[k]) {
                bad++; us(qall_name[k]); us(" got "); uh(f2b(out[r]));
                us(" want "); uh(q8k_dot[k]); us("\n");
            }
        f = __qdot(qall_blk[k], a, QALL_NB, 0.0f);
        d = out[0] - f;
        if (d < 0.0f) d = -d;
        if (!(d <= q8k_tol[k])) {
            bad++; us(qall_name[k]); us(" vs f32 "); uh(f2b(f)); us("\n");
        }
    }

    /* 6. q4_1 pairs with Q8_1, which TVX does not implement */
    __qsetfmt(3);
    __vsetfmt(3);
    __vsetlen(1);
    out[0] = 12345.0f;
    r = __qgemm(out, &prm, QALL_NB);
    if (r != 0 || out[0] != 12345.0f) { bad++; us("q4_1 not refused\n"); }

    __vsetfmt(0);
    us(bad ? "Q8K-FAIL\n" : "Q8K-OK\n");
    return bad;
}
