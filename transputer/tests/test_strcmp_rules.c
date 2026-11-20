/* Expected: SSTRRULESOK\nH */
/*
 * test_strcmp_rules.c - three scheduler/peephole rules found in a word-at-
 * a-time strcmp (transputer-fnopt.h, 2026-11-20):
 *
 *   S16 (TCC_TRANSPUTER_COPY_PARK)  stl t; ldl t; stl p; ldl t -> stl p;
 *        ldl p: `x = *++p` reads the new pointer from p
 *   not  (TCC_TRANSPUTER_NOT_FOLD)  ldc -1; xor -> not (`~w`)
 *   cj reuse (TCC_TRANSPUTER_CJ_REUSE)  R; cj L; R -> R; dup; cj L: the
 *        value `while (*a && *a == *b)` tests is reused on its way on
 *
 * Word-at-a-time strcmp/strlen and a byte loop, over random strings of
 * every length 0..23 at every alignment, against byte-at-a-time
 * references that read through volatile pointers.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }

typedef unsigned int u32;
#define HAS_ZERO(w) ((((w) - 0x01010101u) & ~(w) & 0x80808080u) != 0u)

static int wcmp(const char *a, const char *b)
{
    if ((((u32)a | (u32)b) & 3u) == 0u) {
        const u32 *aw = (const u32 *)a;
        const u32 *bw = (const u32 *)b;
        u32 x = *aw;
        while (x == *bw && !HAS_ZERO(x)) {
            x = *++aw;
            ++bw;
        }
        a = (const char *)aw;
        b = (const char *)bw;
    }
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int wlen(const char *s)
{
    const char *p = s;
    while (((u32)p & 3u) != 0u) {
        if (!*p)
            return (int)(p - s);
        p++;
    }
    {
        const u32 *w = (const u32 *)p;
        u32 x = *w;
        while (!HAS_ZERO(x))
            x = *++w;
        p = (const char *)w;
    }
    while (*p)
        p++;
    return (int)(p - s);
}

static unsigned inv(unsigned x) { return x ^ 0xffffffffu; }

static int rcmp(const volatile char *a, const volatile char *b)
{
    int i = 0;
    for (;;) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x != y || !x)
            return x - y;
        i++;
    }
}

static int rlen(const volatile char *s)
{
    int i = 0;
    while (s[i])
        i++;
    return i;
}

static unsigned seed = 99;
static unsigned rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 16;
}

static int sgn(int x) { return (x > 0) - (x < 0); }

int test_main(void)
{
    static u32 abuf[16], bbuf[16];
    char *A = (char *)abuf, *B = (char *)bbuf;
    int bad = 0, k, len, oa, ob, i;

    for (k = 0; k < 600; k++) {
        len = (int)(rnd() % 24);
        oa = (int)(rnd() & 3);
        ob = (rnd() & 1) ? oa : (int)(rnd() & 3);
        for (i = 0; i < 64; i++) {
            A[i] = (char)(rnd() | 1);
            B[i] = (char)(rnd() | 1);
        }
        for (i = 0; i < len; i++)
            A[oa + i] = B[ob + i] = (char)(0x41 + (rnd() % 3) * 0x41);
        A[oa + len] = 0;
        if (rnd() & 1)
            B[ob + len] = 0;
        else if (len)
            B[ob + (int)(rnd() % len)] = (char)0xC1;
        if (sgn(wcmp(A + oa, B + ob)) != sgn(rcmp(A + oa, B + ob)))
            bad++;
        if (wlen(A + oa) != rlen(A + oa))
            bad++;
        if (inv((u32)k) != ~(u32)k)
            bad++;
    }
    us(bad ? "STRRULESFAIL\n" : "STRRULESOK\n");
    return 0;
}
