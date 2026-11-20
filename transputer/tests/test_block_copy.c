/* Expected: SBLOCKCOPYOK\nH */
/*
 * test_block_copy.c - block-local copy propagation and dead post-increments.
 *
 * tps_block_copies (transputer-fnopt.h) lets a read of y after
 * `ldl x; stl y` (or `ldl x; ldnlp k; stl y`) read x instead, up to the end
 * of the basic block, while neither is written; tp_stmt_end
 * (transputer-fp.h) deletes the saved value of a post-increment used as a
 * statement.  The cases: `a[i] += v` in loops, `t[k]++` through a pointer
 * parameter, a copy whose source is written before the copy's last read,
 * a local whose address a pointer holds and is written through it between
 * the copy and the read, a call between, volatile post-increments (their
 * read must stay), and index expressions with side effects (once only).
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

static void mat(int n, int *c, const short *a, const short *b)
{
    int i, j, k;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            c[i * n + j] = 0;
            for (k = 0; k < n; k++)
                c[i * n + j] += a[i * n + k] * b[k * n + j];
        }
}

static void counts(unsigned *tc, const char *s)
{
    for (; *s; s++) {
        tc[0]++;
        if (*s == 'a')
            tc[1]++;
        else
            tc[2]--;
        tc[*s & 3]++;
    }
}

static int calls;
static int side(int k) { calls++; return k; }

static int written(int *p)
{
    int x = *p, y;
    int *q = &x;
    y = x;              /* copy of x */
    *q = 7;             /* x changes through a pointer */
    return y * 100 + x; /* y keeps the old value */
}

static int redefined(int a, int b)
{
    int y = a;
    a = b;              /* the copy's source is written */
    return y * 10 + a;
}

static int g_seen;
static int peek(int v) { g_seen = v; return 1; }

static int across(int *p)
{
    int *q = p + 1;
    int r = q[0];
    peek(r);
    r += q[1];
    return r;
}

void test_main(void)
{
    short a[9], b[9];
    int c[9];
    unsigned tc[4];
    volatile unsigned vc[2];
    int i, x = 5;

    for (i = 0; i < 9; i++) {
        a[i] = (short)(i + 1);
        b[i] = (short)(9 - i);
    }
    mat(3, c, a, b);
    CHK(c[0], 1 * 9 + 2 * 6 + 3 * 3);
    CHK(c[4], 4 * 8 + 5 * 5 + 6 * 2);
    CHK(c[8], 7 * 7 + 8 * 4 + 9 * 1);

    for (i = 0; i < 4; i++)
        tc[i] = 100;
    counts(tc, "abacab");
    CHK(tc[0], 100 + 6);              /* no char has &3 == 0 */
    CHK(tc[1], 100 + 3 + 3);
    CHK(tc[2], 100 - 3 + 2);
    CHK(tc[3], 100 + 1);

    vc[0] = 1;
    vc[1] = 2;
    vc[0]++;
    vc[1]--;
    CHK(vc[0] + vc[1] * 16, 2 + 16);

    calls = 0;
    tc[side(1)]++;
    tc[side(2)]--;
    CHK(calls, 2);
    CHK(tc[1] + tc[2] * 1000, 107 + 98 * 1000);

    CHK(written(&x), 507);
    CHK(redefined(3, 4), 34);
    c[1] = 40;
    c[2] = 2;
    CHK(across(c), 42);
    CHK(g_seen, 40);

    us(bad ? "\nBLOCKCOPYFAIL\n" : "BLOCKCOPYOK\n");
}
