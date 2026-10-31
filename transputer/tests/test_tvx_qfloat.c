/*
 * Target: -mt9000tvx
 * test_tvx_qfloat.c - QDOT and QGEMM with unquantised weights.
 * Expected: SQFLOAT-OK\nH
 *
 * F32 (ggml type 0), F16 (1) and BF16 (30) weights are taken in blocks of
 * 32, so a router or a small float projection runs through QGEMM like any
 * quantised matrix.  The oracle is the order TVX defines, computed by plain
 * C on the T800 FPU: per 32-element block t = fl32(t + fl32(w * a)) in
 * element order, then acc = fl32(acc + t).  The weights are exact in all
 * three formats, so every format must match the oracle bit for bit, with
 * f32 activations and with bf16 ones.  The integer path (__vsetfmt(3)) has
 * no kernel for float weights and must refuse them.
 */
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}
static float b2f(unsigned u){union{float f;unsigned u;}v;v.u=u;return v.f;}

void __vsetlen(int n);
void __vsetfmt(unsigned fmt);
void __qsetfmt(int ggml_type);
int __qgemm(float *dst, const void *param, unsigned nblocks);
float __qdot(const void *blocks, const float *acts, int nblocks, float acc);
unsigned __f32tobf16(unsigned f32_bits);
unsigned __f32tof16(unsigned f32_bits);
unsigned __bf16tof32(unsigned bf16_bits);

#define ROWS 3
#define K    96                         /* three blocks of 32 */
static float wf[ROWS * K], a[K], a2[K], ref[ROWS], ref2[ROWS], out[ROWS];
static unsigned short wh[ROWS * K], wb[ROWS * K], abf[K];
static struct { const void *acts; const void *weights; } prm;

static void oracle(float *dst, const float *x)
{
    int r, b, j;
    for (r = 0; r < ROWS; r++) {
        float acc = 0.0f;
        for (b = 0; b < K / 32; b++) {
            float t = 0.0f;
            for (j = 0; j < 32; j++) {
                float p = wf[r * K + b * 32 + j] * x[b * 32 + j];
                t = t + p;
            }
            acc = acc + t;
        }
        dst[r] = acc;
    }
}

int test_main(void)
{
    static const int fmt[3] = { 0, 1, 30 };
    const void *wts[3];
    int i, k, r, bad = 0;

    for (i = 0; i < ROWS * K; i++) {
        wf[i] = (float)(((i * 13) % 41) - 20) * 0.03125f;
        wh[i] = (unsigned short)__f32tof16(f2b(wf[i]));
        wb[i] = (unsigned short)__f32tobf16(f2b(wf[i]));
    }
    for (i = 0; i < K; i++) {
        a[i] = (float)((i * 7) % 23 - 11) * 0.1f;
        abf[i] = (unsigned short)__f32tobf16(f2b(a[i]));
        a2[i] = b2f(__bf16tof32(abf[i]));
    }
    oracle(ref, a);
    oracle(ref2, a2);
    wts[0] = wf; wts[1] = wh; wts[2] = wb;

    for (k = 0; k < 3; k++) {
        float f;

        __qsetfmt(fmt[k]);
        __vsetlen(ROWS);
        prm.weights = wts[k];

        __vsetfmt(0);                   /* f32 activations */
        prm.acts = a;
        r = __qgemm(out, &prm, K / 32);
        if (r != ROWS) { bad++; us("rows\n"); }
        for (r = 0; r < ROWS; r++)
            if (f2b(out[r]) != f2b(ref[r])) {
                bad++; uh(fmt[k]); us(" f32 got "); uh(f2b(out[r]));
                us(" want "); uh(f2b(ref[r])); us("\n");
            }

        __vsetfmt(1);                   /* bf16 activations */
        prm.acts = abf;
        r = __qgemm(out, &prm, K / 32);
        for (r = 0; r < ROWS; r++)
            if (f2b(out[r]) != f2b(ref2[r])) {
                bad++; uh(fmt[k]); us(" bf16 got "); uh(f2b(out[r]));
                us(" want "); uh(f2b(ref2[r])); us("\n");
            }

        __vsetfmt(0);
        f = __qdot(wts[k], a, K / 32, 0.0f);
        if (f2b(f) != f2b(ref[0])) { bad++; uh(fmt[k]); us(" qdot\n"); }

        __vsetfmt(3);                   /* no integer kernel */
        out[0] = 12345.0f;
        r = __qgemm(out, &prm, K / 32);
        if (r != 0 || out[0] != 12345.0f) { bad++; uh(fmt[k]); us(" int\n"); }
    }

    __vsetfmt(0);
    us(bad ? "QFLOAT-FAIL\n" : "QFLOAT-OK\n");
    return bad;
}
