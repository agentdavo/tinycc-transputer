/* Expected: SUCMPLITOK\nH */
/*
 * test_unsigned_cmp_ldiff.c - unsigned compares on the parts without gtu.
 *
 * The T800, T805, T425 and T414 flipped both sign bits and used gt; when
 * both operands are short replayable pushes, tp_emit_gtu now pushes a zero
 * under them and takes ldiff's borrow.  Every relation, both ways round,
 * as a value and as a branch, for literals at and around 0, the sign
 * boundary and all ones, is checked against the same compare with the
 * literal in a volatile (pushed by a load the rewrite does not replay);
 * then two locals, members through a pointer and an array element at a
 * variable index (which falls back), against the same values held in
 * volatiles.
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
static void chk(int line, unsigned u, int got, int want)
{
    if (got != want) {
        bad++;
        us("\nFAIL line ");
        uh((unsigned)line);
        us(" u=");
        uh(u);
    }
}

static volatile unsigned vk;

#define REL(u, K)                                                           \
    do {                                                                    \
        vk = (K);                                                           \
        chk(__LINE__, u, (u) <  (K), (u) <  vk);                            \
        chk(__LINE__, u, (u) <= (K), (u) <= vk);                            \
        chk(__LINE__, u, (u) >  (K), (u) >  vk);                            \
        chk(__LINE__, u, (u) >= (K), (u) >= vk);                            \
        chk(__LINE__, u, (K) <  (u), vk <  (u));                            \
        chk(__LINE__, u, (K) <= (u), vk <= (u));                            \
        chk(__LINE__, u, (K) >  (u), vk >  (u));                            \
        chk(__LINE__, u, (K) >= (u), vk >= (u));                            \
        if ((u) < (K)) n++;                                                 \
        if ((u) >= (K)) n++;                                                \
        if ((K) > (u)) n++;                                                 \
        if ((K) <= (u)) n++;                                                \
    } while (0)

#define ALLK(u)                                                             \
    do {                                                                    \
        REL(u, 0u); REL(u, 1u); REL(u, 2u); REL(u, 319u); REL(u, 320u);     \
        REL(u, 0x7ffffffeu); REL(u, 0x7fffffffu); REL(u, 0x80000000u);      \
        REL(u, 0x80000001u); REL(u, 0xfffffffeu); REL(u, 0xffffffffu);      \
    } while (0)

void test_main(void)
{
    static const unsigned vals[] = {
        0, 1, 2, 319, 320, 321, 0x7ffffffeu, 0x7fffffffu, 0x80000000u,
        0x80000001u, 0xfffffffeu, 0xffffffffu, 0x12345678u, 0xdeadbeefu
    };
    unsigned i;
    int n = 0;

    for (i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        unsigned u = vals[i];
        ALLK(u);
    }
    /* each literal satisfies exactly two of the four branches per value */
    chk(__LINE__, n, n, 2 * 11 * (int)(sizeof vals / sizeof vals[0]));

    /* two variables, members through a pointer, a variable index */
    for (i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        unsigned j;
        for (j = 0; j < sizeof vals / sizeof vals[0]; j++) {
            struct { unsigned a, b; } s, *p = &s;
            unsigned x = vals[i], y = vals[j];
            volatile unsigned vx = x, vy = y;
            s.a = x;
            s.b = y;
            chk(__LINE__, x, x < y, vx < vy);
            chk(__LINE__, x, x <= y, vx <= vy);
            chk(__LINE__, x, x > y, vx > vy);
            chk(__LINE__, x, x >= y, vx >= vy);
            chk(__LINE__, x, p->a < p->b, vx < vy);
            chk(__LINE__, x, p->b >= p->a, vy >= vx);
            chk(__LINE__, x, p->a > y, vx > vy);
            chk(__LINE__, x, vals[i] <= vals[j], vx <= vy);
            if (p->a < y) n++;
            if (x >= p->b) n++;
        }
    }
    chk(__LINE__, n, n, 2 * 11 * 14 + 14 * 14);
    us(bad ? "\nUCMPLITFAIL\n" : "UCMPLITOK\n");
}
