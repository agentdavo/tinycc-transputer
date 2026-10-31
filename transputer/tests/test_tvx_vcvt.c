/*
 * Target: -mt9000tvx
 * test_tvx_vcvt.c - VCVT, the vector float-format convert.
 * Expected: SVCVT-OK\nH
 *
 * Every conversion VCVT offers - f32, bf16 and f16, each to each - checked
 * element by element against the scalar TVX conversions it must agree with
 * (R32TOBF16, BF16TOR32, R32TOF16, F16TOR32), on values that hit rounding
 * ties both ways, subnormals, overflow, Inf and NaN.
 */
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}

unsigned __f32tobf16(unsigned f32_bits);
unsigned __bf16tof32(unsigned bf16_bits);
unsigned __f32tof16(unsigned f32_bits);
unsigned __f16tof32(unsigned f16_bits);
void __vsetlen(int n);
void __vsetfmt(unsigned fmt);
void __vcvt(void *dst, const void *src, unsigned dst_fmt);

#define N 64
static const unsigned edge[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbf800000,
    0x3f808000, 0x3f818000, 0x3f80c000, 0x7f7fffff,     /* bf16 ties, max */
    0x7f800000, 0xff800000, 0x7fc00000, 0x7f800001,     /* Inf, NaN, sNaN */
    0x00000001, 0x00400000, 0x387fc000, 0x33000000,     /* subnormals */
    0x477fe000, 0x477ff000, 0x47800000, 0x38800000,     /* f16 max, tie, ovf */
    0x3a801000, 0x3a802000, 0xc2f6e979, 0x3eaaaaab
};
static unsigned a32[N], o32[N];
static unsigned short a16[N], b16[N], o16[N];

static void fail(const char *what, int i, unsigned got, unsigned want, int *bad)
{
    if (*bad < 8) {
        us(what); us(" ["); uh((unsigned)i); us("] got "); uh(got);
        us(" want "); uh(want); us("\n");
    }
    (*bad)++;
}

int test_main(void)
{
    int i, bad = 0;
    unsigned h = 0x9e3779b9u;

    for (i = 0; i < N; i++) {
        if (i < (int)(sizeof edge / sizeof edge[0]))
            a32[i] = edge[i];
        else {
            h ^= h << 13; h ^= h >> 17; h ^= h << 5;
            a32[i] = h;
        }
    }
    __vsetlen(N);

    __vsetfmt(0);                               /* from f32 */
    __vcvt(o16, a32, 1);
    for (i = 0; i < N; i++)
        if (o16[i] != (__f32tobf16(a32[i]) & 0xffff))
            fail("f32>bf16", i, o16[i], __f32tobf16(a32[i]) & 0xffff, &bad);
    for (i = 0; i < N; i++)
        b16[i] = o16[i];
    __vcvt(o16, a32, 2);
    for (i = 0; i < N; i++)
        if (o16[i] != (__f32tof16(a32[i]) & 0xffff))
            fail("f32>f16", i, o16[i], __f32tof16(a32[i]) & 0xffff, &bad);
    for (i = 0; i < N; i++)
        a16[i] = o16[i];
    __vcvt(o32, a32, 0);
    for (i = 0; i < N; i++)
        if (o32[i] != a32[i])
            fail("f32>f32", i, o32[i], a32[i], &bad);

    __vsetfmt(1);                               /* from bf16 */
    __vcvt(o32, b16, 0);
    for (i = 0; i < N; i++)
        if (o32[i] != __bf16tof32(b16[i]))
            fail("bf16>f32", i, o32[i], __bf16tof32(b16[i]), &bad);
    __vcvt(o16, b16, 2);
    for (i = 0; i < N; i++)
        if (o16[i] != (__f32tof16(__bf16tof32(b16[i])) & 0xffff))
            fail("bf16>f16", i, o16[i], __f32tof16(__bf16tof32(b16[i])) & 0xffff, &bad);

    __vsetfmt(2);                               /* from f16 */
    __vcvt(o32, a16, 0);
    for (i = 0; i < N; i++)
        if (o32[i] != __f16tof32(a16[i]))
            fail("f16>f32", i, o32[i], __f16tof32(a16[i]), &bad);
    __vcvt(o16, a16, 1);
    for (i = 0; i < N; i++)
        if (o16[i] != (__f32tobf16(__f16tof32(a16[i])) & 0xffff))
            fail("f16>bf16", i, o16[i], __f32tobf16(__f16tof32(a16[i])) & 0xffff, &bad);

    __vsetfmt(0);
    us(bad ? "VCVT-FAIL\n" : "VCVT-OK\n");
    return bad;
}
