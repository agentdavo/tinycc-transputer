/*
 * Target: -mt9000tvx
 * test_tvx.c - on-target conformance test for the TVX instruction group
 * (MXFP4 / bfloat16) and the __bf16 storage type.
 *
 * Every expected value in tvx_vectors.inc is produced by tvx_genvec.c from
 * tvx_ref.h, the independent spec-derived reference model.  This test
 * therefore checks the *hardware* against the *specification*.
 *
 * Build & run:
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/tvx.o transputer/tests/test_tvx.c
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/boot.o transputer/tests/test_boot.S
 *   ./transputer-tcc -nostdlib -nostdinc -o /tmp/tvx.elf /tmp/boot.o /tmp/tvx.o
 *   ./transputer-netload /tmp/tvx.elf /tmp/tvx.btl
 *   DISPLAY= timeout 60 transputer/t4/t4 -s8 -si -se -sb /tmp/tvx.btl
 */

#include "tvx_vectors.inc"

#define UART_DATA (*(volatile unsigned int *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_puthex(unsigned int v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int nib = (v >> i) & 0xF;
        uart_putc(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
}
static void uart_putdec(unsigned int v)
{
    char b[12];
    int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { b[n++] = '0' + (v % 10); v /= 10; }
    while (n) uart_putc(b[--n]);
}

static int failures = 0;
static int checks = 0;

static void expect(const char *what, unsigned int in,
                   unsigned int got, unsigned int want)
{
    checks++;
    if (got != want) {
        failures++;
        uart_puts("\nFAIL ");
        uart_puts(what);
        uart_puts(" in=");   uart_puthex(in);
        uart_puts(" got=");  uart_puthex(got);
        uart_puts(" want="); uart_puthex(want);
    }
}

/* reinterpretation helpers (no arithmetic, so no rounding involved) */
static unsigned int f2b(float f) { union { float f; unsigned int u; } v; v.f = f; return v.u; }
static float b2f(unsigned int u) { union { float f; unsigned int u; } v; v.u = u; return v.f; }

/* ------------------------------------------------------------------ */
/* MXFP4 block used by the dot-product test.                           */
/* Layout: byte 0 = E8M0 scale, bytes 1..16 = 32 E2M1 nibbles.         */
/* ------------------------------------------------------------------ */
static unsigned char blk[17];
static float act[32];
static unsigned short bx[24], by[24];

int test_main(void)
{
    int i;
    unsigned int r;

    /* ---------------- scalar conversions ---------------- */

    for (i = 0; i < 16; i++) {
        r = __mxfp4tof32((unsigned int)i);
        expect("mxfp4->f32", (unsigned int)i, r, tvx_mxfp4_dec[i]);
    }

    for (i = 0; i < TVX_NE8; i++) {
        r = __mxe8m0tof32(tvx_e8_in[i]);
        expect("e8m0->f32", tvx_e8_in[i], r, tvx_e8_dec[i]);
    }

    for (i = 0; i < TVX_NVEC; i++) {
        r = __f32tobf16(tvx_in[i]);
        expect("f32->bf16", tvx_in[i], r, tvx_bf16[i]);

        r = __f32tomxfp4(tvx_in[i]);
        expect("f32->mxfp4", tvx_in[i], r, tvx_mxfp4[i]);

        r = __mxe8m0of(tvx_in[i]);
        expect("e8m0of", tvx_in[i], r, tvx_e8m0of[i]);

        /* widening bf16 is exact: it is a pure left shift by 16 */
        r = __bf16tof32(tvx_bf16[i]);
        expect("bf16->f32", tvx_bf16[i], r, tvx_bf16[i] << 16);
    }

    /* pack / unpack */
    for (i = 0; i + 1 < TVX_NVEC; i += 2) {
        unsigned int p = __bf16pack(tvx_in[i], tvx_in[i + 1]);
        unsigned int want = (tvx_bf16[i] << 16) | tvx_bf16[i + 1];
        expect("bf16pack", tvx_in[i], p, want);
        expect("bf16unpkh", p, __bf16unpkh(p), tvx_bf16[i] << 16);
        expect("bf16unpkl", p, __bf16tof32(p), tvx_bf16[i + 1] << 16);
    }

    /* ---------------- the __bf16 storage type ---------------- */

    {
        __bf16 a, b;
        __bf16 arr[4];
        float f;

        expect("sizeof __bf16", 0, (unsigned int)sizeof(__bf16), 2u);
        expect("sizeof arr", 0, (unsigned int)sizeof(arr), 8u);

        /* constant conversion is folded at compile time */
        a = 1.5f;
        expect("bf16 const", 0x3FC00000u, f2b((float)a), 0x3FC00000u);

        /* runtime narrowing then widening */
        f = 3.14159265f;
        b = (__bf16)f;
        expect("bf16 rt narrow", f2b(f), f2b((float)b),
               __bf16tof32(__f32tobf16(f2b(f))));

        /* arithmetic promotes to binary32 and computes in float */
        a = 1.0f;
        b = 0.5f;
        f = (float)a + (float)b;
        expect("bf16 add", 0, f2b(f), 0x3FC00000u);

        /* implicit promotion: no casts written at all */
        f = a * b;
        expect("bf16 mul promo", 0, f2b(f), f2b(0.5f));

        /* comparison also promotes */
        expect("bf16 cmp", 0, (unsigned int)(a > b), 1u);

        /* array store/load round-trip through 2-byte memory */
        arr[0] = 1.0f; arr[1] = 2.0f; arr[2] = -0.5f; arr[3] = 6.0f;
        expect("bf16 arr0", 0, f2b((float)arr[0]), 0x3F800000u);
        expect("bf16 arr1", 0, f2b((float)arr[1]), 0x40000000u);
        expect("bf16 arr2", 0, f2b((float)arr[2]), 0xBF000000u);
        expect("bf16 arr3", 0, f2b((float)arr[3]), 0x40C00000u);
    }

    /* ---------------- MXFP4DOT ---------------- */

    {
        float ref, got, acc;
        unsigned int seed = 2463534242u;

        blk[0] = 130;               /* E8M0 = 2^3 = 8.0 */
        for (i = 0; i < 16; i++) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            blk[1 + i] = (unsigned char)(seed >> 13);
        }
        for (i = 0; i < 32; i++) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            act[i] = ((float)(int)((seed >> 22) & 1023) - 512.0f) / 64.0f;
        }

        /* reference: sequential binary32 accumulation, scale applied last */
        ref = 0.0f;
        for (i = 0; i < 32; i++) {
            unsigned int nib;   /* GGUF split-half order */
            if (i < 16) nib = (unsigned int)(blk[1 + i] & 0x0F);
            else        nib = (unsigned int)(blk[1 + i - 16] >> 4);
            ref = ref + b2f(__mxfp4tof32(nib)) * act[i];
        }
        acc = 0.25f;
        ref = acc + b2f(__mxe8m0tof32(blk[0])) * ref;

        got = __mxfp4dot(blk, act, acc);
        expect("mxfp4dot", 0, f2b(got), f2b(ref));

        /* accumulating a second block must chain correctly */
        got = __mxfp4dot(blk, act, got);
        ref = ref + (ref - acc);
        expect("mxfp4dot chain", 0, f2b(got), f2b(ref));
    }

    /* ---------------- BF16DOT ---------------- */

    {
        float ref, got;
        for (i = 0; i < 24; i++) {
            bx[i] = (unsigned short)__f32tobf16(f2b((float)(i - 12) * 0.75f));
            by[i] = (unsigned short)__f32tobf16(f2b((float)(i + 1) * 0.5f));
        }
        ref = 0.0f;
        for (i = 0; i < 24; i++)
            ref = ref + b2f(__bf16tof32(bx[i])) * b2f(__bf16tof32(by[i]));
        ref = 1.0f + ref;

        got = __bf16dot(bx, by, 24, 1.0f);
        expect("bf16dot", 0, f2b(got), f2b(ref));

        /* n = 0 must leave the accumulator untouched */
        got = __bf16dot(bx, by, 0, 2.5f);
        expect("bf16dot n=0", 0, f2b(got), f2b(2.5f));
    }

    /* ---------------- summary ---------------- */

    uart_puts("\nchecks="); uart_putdec((unsigned int)checks);
    uart_puts(" failures="); uart_putdec((unsigned int)failures);
    uart_puts(failures ? "\nTVX: FAIL\n" : "\nTVX: PASS\n");
    return failures ? 1 : 0;
}
