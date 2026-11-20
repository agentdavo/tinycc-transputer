/* Expected: SSCHEDHOISTOK\nH */
/*
 * test_sched_hoist.c - four ways the back end now avoids a REV or a J.
 *
 *  1. Unsigned relations swap their operands towards the form the part
 *     compares without REV (`gtu` on the T9000, mint/xor elsewhere): every
 *     relation at the edge values, as a value, a condition and a loop test,
 *     against signed compares of the halved words.
 *  2. The scheduler hoists a pure push over the run that computed the value
 *     under it (`ldc 0; ldl i; ldl p; wsub; stnl 0`, no REV): word, byte and
 *     struct fills of constants and of variables, indices loaded from
 *     memory, and the loop head label landing on the hoisted push.
 *  3. A shift count masked with `& k` (k <= 31) in the source keeps no
 *     clamp of its own: rotates and masked shifts at counts 0..63 against
 *     single-bit steps.
 *  4. `cj L1; j L2; L1:` becomes `eqc 0; cj L2` (a `?:` whose first arm
 *     has no code): constant and variable arms, nested, in loops and in
 *     conditions, against if/else.
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

/* ---- 1. unsigned relations ---- */

static const unsigned edge[] = {
    0, 1, 2, 0x7ffffffeu, 0x7fffffffu, 0x80000000u, 0x80000001u,
    0xfffffffeu, 0xffffffffu
};
#define NEDGE (sizeof(edge) / sizeof(edge[0]))

/* a < b by signed compares of the top 31 bits, then the low bit. */
static int ref_lt(unsigned a, unsigned b)
{
    int ha = (int)(a >> 1), hb = (int)(b >> 1);
    if (ha != hb)
        return ha < hb;
    return (int)(a & 1) < (int)(b & 1);
}

static unsigned rel_bits(unsigned a, unsigned b)
{
    unsigned r = 0;
    if (a < b)  r |= 1;
    if (a <= b) r |= 2;
    if (a > b)  r |= 4;
    if (a >= b) r |= 8;
    r |= (unsigned)(a < b) << 4;
    r |= (unsigned)(a <= b) << 5;
    r |= (unsigned)(a > b) << 6;
    r |= (unsigned)(a >= b) << 7;
    /* register operands, not locals */
    r |= (unsigned)((a ^ 0u) < (b | 0u)) << 8;
    r |= (unsigned)((a + 0u) >= (b ^ 0u)) << 9;
    return r;
}

static unsigned ref_bits(unsigned a, unsigned b)
{
    unsigned lt = ref_lt(a, b), gt = ref_lt(b, a);
    unsigned r = lt | (!gt << 1) | (gt << 2) | (!lt << 3);
    return r | (r << 4) | (lt << 8) | (!lt << 9);
}

static unsigned count_up(unsigned lo, unsigned hi)
{
    unsigned i, n = 0;
    for (i = lo; i < hi; i++)
        n++;
    return n;
}

static unsigned count_down(unsigned hi, unsigned lo)
{
    unsigned i = hi, n = 0;
    while (i >= lo && n < 100) {
        n++;
        if (i == 0)
            break;
        i--;
    }
    return n;
}

static void test_relations(void)
{
    unsigned i, j;
    for (i = 0; i < NEDGE; i++)
        for (j = 0; j < NEDGE; j++)
            chk(__LINE__ * 256 + i * 16 + j, rel_bits(edge[i], edge[j]),
                ref_bits(edge[i], edge[j]));
    chk(__LINE__, count_up(0, 0), 0);
    chk(__LINE__, count_up(0, 7), 7);
    chk(__LINE__, count_up(0xfffffffau, 0xffffffffu), 5);
    chk(__LINE__, count_up(0x7ffffffdu, 0x80000002u), 5);
    chk(__LINE__, count_down(5, 3), 3);
    chk(__LINE__, count_down(3, 0), 4);
    chk(__LINE__, count_down(0x80000001u, 0x7fffffffu), 3);
}

/* ---- 2. pushes hoisted over a value run ---- */

struct rec { unsigned pad; unsigned w[8]; unsigned char b[8]; };

static unsigned wfill[16];
static unsigned char bfill[16];
static struct rec rfill;
static const unsigned char perm[8] = { 3, 0, 7, 5, 1, 6, 2, 4 };

static unsigned sum_words(const unsigned *p, unsigned n)
{
    unsigned s = 0, i;
    for (i = 0; i < n; i++)
        s = s * 31u + p[i];
    return s;
}

static unsigned ref_sum(unsigned n, unsigned (*f)(unsigned))
{
    unsigned s = 0, i;
    for (i = 0; i < n; i++)
        s = s * 31u + f(i);
    return s;
}

static unsigned f_zero(unsigned i) { return 0 * i; }
static unsigned f_k(unsigned i) { return 0xc0c0c0u + 0 * i; }
static unsigned f_i3(unsigned i) { return i * 3u; }
static unsigned f_perm(unsigned i)
{
    unsigned k;
    for (k = 0; k < 8; k++)
        if (perm[k] == i)
            return 7u;
    return 0x55u;
}

static void fill_zero(unsigned *p, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        p[i] = 0;
}

static void fill_val(unsigned *p, unsigned n, unsigned v)
{
    unsigned i;
    for (i = 0; i < n; i++)
        p[i] = v;
}

static void test_hoist(void)
{
    unsigned i, k, n = 16;
    unsigned *p = wfill;

    fill_val(wfill, 16, 0xffffffffu);
    fill_zero(wfill, 16);
    chk(__LINE__, sum_words(wfill, 16), ref_sum(16, f_zero));
    fill_val(wfill, 16, 0xc0c0c0u);
    chk(__LINE__, sum_words(wfill, 16), ref_sum(16, f_k));

    /* the pushed value is the loop variable the run also reads */
    for (i = 0; i < n; i++)
        p[i] = i * 3u;
    chk(__LINE__, sum_words(wfill, 16), ref_sum(16, f_i3));
    for (i = 0; i < n; i++)
        p[i] = 0;
    for (i = 0; i < n; i++)
        p[i] = i;
    for (i = 0; i < n; i++)
        chk(__LINE__ * 256 + i, wfill[i], i);

    /* index loaded from memory: the run has an lb */
    fill_val(wfill, 8, 0x55u);
    for (i = 0; i < 8; i++)
        p[perm[i]] = 7;
    chk(__LINE__, sum_words(wfill, 8), ref_sum(8, f_perm));

    /* a variable that changes inside the loop */
    k = 100;
    for (i = 0; i < 8; i++) {
        p[i + 1] = k;
        k += 11;
    }
    for (i = 0; i < 8; i++)
        chk(__LINE__ * 256 + i, wfill[i + 1], 100 + 11 * i);

    /* struct members and bytes */
    for (i = 0; i < 8; i++) {
        rfill.w[i] = 0x01020304u;
        rfill.b[i] = 0x5a;
        bfill[i * 2] = 0;
        bfill[i * 2 + 1] = 0xa5;
    }
    for (i = 0; i < 8; i++) {
        chk(__LINE__ * 256 + i, rfill.w[i], 0x01020304u);
        chk(__LINE__ * 256 + i, rfill.b[i], 0x5a);
        chk(__LINE__ * 256 + i, bfill[i * 2], 0);
        chk(__LINE__ * 256 + i, bfill[i * 2 + 1], 0xa5);
    }
    chk(__LINE__, rfill.pad, 0);

    /* three deep: the value is wanted under two others */
    for (i = 0; i < 8; i++)
        wfill[i] = 0;
    for (i = 0; i < 8; i++)
        wfill[(i * 5 + 3) & 7] = (i + 1) * (i + 2);
    for (i = 0; i < 8; i++) {
        unsigned j;
        for (j = 0; j < 8; j++)
            if (((j * 5 + 3) & 7) == i)
                chk(__LINE__ * 256 + i, wfill[i], (j + 1) * (j + 2));
    }
}

/* ---- 3. masked shift counts ---- */

static unsigned rol32(unsigned w, unsigned s)
{
    return (w << (s & 31)) | (w >> ((-s) & 31));
}

static unsigned ror32(unsigned w, unsigned s)
{
    return (w >> (s & 31)) | (w << ((-s) & 31));
}

static unsigned rol1(unsigned w, unsigned n)
{
    while (n--)
        w = (w << 1) | (w >> 31);
    return w;
}

static void test_shifts(void)
{
    unsigned s, w = 0x81234567u;
    int x = -0x12345;
    for (s = 0; s < 64; s++) {
        unsigned c = s & 31, m7 = s & 7;
        chk(__LINE__ * 256 + s, rol32(w, s), rol1(w, s & 31));
        chk(__LINE__ * 256 + s, ror32(w, s), rol1(w, (32 - (s & 31)) & 31));
        chk(__LINE__ * 256 + s, w << (s & 7), w * (1u << m7));
        chk(__LINE__ * 256 + s, w >> (s & 15),
            (unsigned)((unsigned long long)w / (1ull << (s & 15))));
        chk(__LINE__ * 256 + s, (unsigned)(x >> (s & 31)),
            (unsigned)(x < 0 ? ~(~(unsigned)x >> c) : (unsigned)x >> c));
    }
}

/* ---- 4. ?: whose first arm has no code ---- */

static unsigned sel_k(unsigned c) { return c ? 0xc0c0c0u : 0; }
static unsigned sel_kk(unsigned c) { return (c & 0x80u) ? 5u : 9u; }
static unsigned sel_v(unsigned c, unsigned a, unsigned b) { return c ? a : b; }

static unsigned ref_sel(unsigned c, unsigned a, unsigned b)
{
    if (c)
        return a;
    return b;
}

static void test_select(void)
{
    unsigned i, x, bits = 0xa5u, out[8], n;
    for (i = 0; i < 4; i++) {
        chk(__LINE__ * 256 + i, sel_k(i & 1), ref_sel(i & 1, 0xc0c0c0u, 0));
        chk(__LINE__ * 256 + i, sel_kk(i << 6), ref_sel((i << 6) & 0x80u, 5, 9));
        chk(__LINE__ * 256 + i, sel_v(i & 2, i, 77), ref_sel(i & 2, i, 77));
    }
    /* the font loop */
    for (x = 0; x < 8; x++)
        out[x] = (bits & (0x80u >> x)) ? 0xc0c0c0u : 0;
    for (x = 0; x < 8; x++) {
        unsigned want = 0;
        if ((0xa5u >> (7 - x)) & 1)
            want = 0xc0c0c0u;
        chk(__LINE__ * 256 + x, out[x], want);
    }
    /* nested, and as a condition */
    n = 0;
    for (i = 0; i < 12; i++) {
        x = (i & 1) ? ((i & 2) ? 1 : 2) : ((i & 4) ? 3 : 4);
        n = n * 5 + x;
        if ((i & 3) ? 0 : 1)
            n += 1000;
    }
    {
        unsigned r = 0;
        for (i = 0; i < 12; i++) {
            if (i & 1)
                x = (i & 2) ? 1 : 2;
            else
                x = (i & 4) ? 3 : 4;
            r = r * 5 + x;
            if ((i & 3) == 0)
                r += 1000;
        }
        chk(__LINE__, n, r);
    }
}

void test_main(void)
{
    test_relations();
    test_hoist();
    test_shifts();
    test_select();
    us(bad ? "\nSCHEDHOISTFAIL\n" : "SCHEDHOISTOK\n");
}
