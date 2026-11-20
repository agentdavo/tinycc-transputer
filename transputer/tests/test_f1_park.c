/* Expected: SF1PARKOK\nH */
/*
 * test_f1_park.c - the F1 workspace rules (transputer-fnopt.h, 2026-11-20):
 *
 *   S13 tps_park_unary    Q; stl a; U; ldl a -> U; Q, U a stack-neutral run
 *                         on the value under Q's (two loads then a diff)
 *   S14 tps_postinc_sink  `*d++ = <R>`: the old d no longer waits in a slot
 *   ext drop (tpn)        xbword / ldc 128; xword before a zero test of a
 *                         byte, or before the byte store of it
 *
 * Every result is checked against the same thing computed with indices and
 * volatile reads, which none of the rules can reach.  Bytes >= 128 are in
 * every string, so a zero test that lost its meaning would show.
 */

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

static int bad;
static void chk(int line, unsigned got, unsigned want)
{
    if (got != want) {
        bad++;
        us("\nFAIL line ");
        uh((unsigned)line);
        us(" got ");
        uh(got);
        us(" want ");
        uh(want);
    }
}
#define CHK(g, w) chk(__LINE__, (unsigned)(g), (unsigned)(w))

/* S14 + ext drop: Dhrystone's strcpy */
static char *cpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}

static signed char *scpy(signed char *d, const signed char *s)
{
    signed char *r = d;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}

/* S14 on words, with arithmetic in R */
static void addk(int *d, const int *s, int n, int k)
{
    while (n-- > 0)
        *d++ = *s++ + k;
}

/* R reads the updated d: S14 must not fire (the stored value is the new
 * d's offset, one more than the old one's) */
static void selfidx(int *d, int *base, int n)
{
    while (n-- > 0) {
        int *o = d;
        d++;
        *o = (int)(d - base) * 3;
    }
}

/* the old pointer used twice: t stays live */
static int twice(char *d, char c)
{
    char *o = d++;
    *o = c;
    return (int)(d - o) + *o;
}

/* S13: two halfword loads through two pointers, then a difference */
struct item { short a, b; };
static int cmpb(struct item *p, struct item *q)
{
    return p->b - q->b;
}
static int cmpsum(short *p, short *q, int n)
{
    int s = 0, i;
    for (i = 0; i < n; i++)
        s += p[i] - q[n - 1 - i];
    return s;
}

/* ext drop before a byte store, sign-extended source */
static void bstore(signed char *d, const signed char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        signed char c = s[i];
        d[i] = c;
    }
}
static void hstore(short *d, const short *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        short c = s[i];
        d[i] = c;
    }
}

/* a zero test of a char that is not just a byte: c + 256 is not zero */
static int notbyte(signed char *s, int k)
{
    int n = 0;
    while (1) {
        signed char c = *s++;
        int w = c + k;
        if ((signed char)w == 0)
            break;
        n++;
    }
    return n;
}

static unsigned hash(const volatile unsigned char *p, int n)
{
    unsigned h = 2166136261u;
    int i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

int test_main(void)
{
    static const char src[] = "ab\x80\xff\x7f\x01z";
    char dst[16];
    static const signed char ssrc[] = { 'q', -1, -128, 127, 1, -2, 0 };
    signed char sdst[16];
    int i;

    for (i = 0; i < 16; i++) dst[i] = 'x';
    CHK(cpy(dst, src) == dst, 1);
    CHK(hash((volatile unsigned char *)dst, 8),
        hash((volatile unsigned char *)src, 8));
    CHK(dst[8], 'x');
    for (i = 0; i < 16; i++) sdst[i] = 5;
    scpy(sdst, ssrc);
    CHK(hash((volatile unsigned char *)sdst, 7),
        hash((volatile unsigned char *)ssrc, 7));
    CHK(sdst[7], 5);
    cpy(dst, "");
    CHK(dst[0], 0);
    CHK(dst[1], 'b');

    {
        int a[6] = { 1, -2, 3, 0x7ffffff0, -0x7ffffff0, 6 }, b[7];
        volatile int *va = a;
        b[6] = 99;
        addk(b, a, 6, 5);
        for (i = 0; i < 6; i++)
            CHK(b[i], va[i] + 5);
        CHK(b[6], 99);
        selfidx(b, b, 6);
        for (i = 0; i < 6; i++)
            CHK(b[i], i * 3 + 3);
        selfidx(b + 2, b, 3);
        CHK(b[1], 6); CHK(b[2], 9); CHK(b[3], 12); CHK(b[4], 15);
        CHK(b[5], 18);
    }
    {
        char t[4] = { 0, 0, 0, 0 };
        CHK(twice(t, 'm'), 1 + 'm');
        CHK(t[0], 'm');
        CHK(t[1], 0);
    }
    {
        struct item x = { 1, -30000 }, y = { 2, 30000 };
        short p[5] = { 1, -2, 32767, -32768, 7 };
        short q[5] = { -9, 300, -32768, 32767, 4 };
        volatile short *vp = p, *vq = q;
        int want = 0;
        CHK(cmpb(&x, &y), -60000);
        CHK(cmpb(&y, &x), 60000);
        for (i = 0; i < 5; i++)
            want += vp[i] - vq[4 - i];
        CHK(cmpsum(p, q, 5), want);
    }
    {
        signed char d8[8];
        short d16[6], s16[6] = { -1, 32767, -32768, 255, -256, 0 };
        volatile signed char *v8 = d8;
        volatile short *v16 = d16;
        bstore(d8, ssrc, 7);
        for (i = 0; i < 7; i++)
            CHK(v8[i], ssrc[i]);
        hstore(d16, s16, 6);
        for (i = 0; i < 6; i++)
            CHK(v16[i], s16[i]);
    }
    {
        static signed char z[] = { 3, 4, -2, 0, 9 };
        CHK(notbyte(z, 0), 3);
        CHK(notbyte(z, 2), 2);      /* -2 + 2 */
        CHK(notbyte(z, 256), 3);    /* (signed char)(c + 256) == c */
    }

    us(bad ? "\nF1PARKFAIL\n" : "F1PARKOK\n");
    return 0;
}
