/*
 * test_vla.c - variable-length arrays.
 *
 * Expected: SVLA-OK\nH
 *
 * Wptr is the transputer's frame pointer and stack pointer at once, so VLAs
 * cannot move it: they come from the running process's arena
 * (lib/transputer/transputer-vla.c).  This checks the cases that used to break when
 * they did move it - locals and parameters next to a VLA, loops, nested
 * scopes, goto, break/continue and return out of VLA scopes, VLAs in called
 * and recursive functions, statement expressions, 2-D VLAs (whose rows
 * were indexed 4 bytes apart) - that every one of them
 * hands its arena space back (__tp_vla_mark() is the same afterwards), and
 * then processes: two low-priority processes and a high-priority one that
 * block in the middle of VLA scopes while the others allocate, each from
 * the bottom of its own workspace, workspaces reused after a process stops
 * or returns, and a process that runs out of arena stopping.
 */

#include <conc.h>

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

char *__tp_vla_mark(void);

static int pass = 1;

static void check(const char *name, int ok)
{
    if (!ok) {
        pass = 0;
        uart_puts("FAIL:");
        uart_puts(name);
        uart_putc('\n');
    }
}

/* ------------------------------------------------------------------ */
/* One process                                                         */
/* ------------------------------------------------------------------ */

/* Six parameters: three arrive in A/B/C, three in the caller's frame.  All
 * of them, and the locals either side of the VLA, must survive it. */
static int frame(int n, int p1, int p2, int p3, int p4, int p5)
{
    int before = 0x1111, *pb = &before;
    int v[n];
    int after = 0x2222;
    int i, s = 0;

    for (i = 0; i < n; i++)
        v[i] = i * 3;
    for (i = 0; i < n; i++)
        s += v[i];
    return s == 3 * n * (n - 1) / 2 && before == 0x1111 && *pb == 0x1111
        && after == 0x2222 && p1 == 11 && p2 == 22 && p3 == 33 && p4 == 44
        && p5 == 55 && (int)sizeof v == n * 4;
}

static int loop_sizes(int iters)
{
    int i, j, s = 0;

    for (i = 0; i < iters; i++) {
        int v[1 + i % 13];
        for (j = 0; j <= i % 13; j++)
            v[j] = i + j;
        s += v[i % 13] - i;
        if (i % 5 == 0)
            continue;               /* leaves the scope early */
        if (i == iters + 1)
            break;
    }
    return s;
}

static int nested(int n)
{
    int ok = 1;
    int outer[n];
    char *inner1, *inner2;
    int i;

    for (i = 0; i < n; i++)
        outer[i] = 100 + i;
    {
        int a[n * 2];
        a[0] = 1;
        inner1 = (char *)a;
        {
            double d[n];            /* 8-byte elements */
            d[n - 1] = 2.5;
            ok &= d[n - 1] == 2.5 && a[0] == 1;
        }
    }
    {
        short b[n * 3];             /* reuses the space a had */
        inner2 = (char *)b;
        b[n * 3 - 1] = 7;
        ok &= b[n * 3 - 1] == 7;
    }
    for (i = 0; i < n; i++)
        ok &= outer[i] == 100 + i;
    return ok && inner1 == inner2;
}

/* goto out of two VLA scopes, and back into a scope above a VLA */
static int jumps(int n)
{
    int i = 0, s = 0;

again:
    {
        int a[n + i];
        a[n + i - 1] = i;
        {
            int b[2 * n];
            b[0] = a[n + i - 1];
            s += b[0];
            if (++i < 5)
                goto again;
            if (i == 5)
                goto out;
        }
        s = -1000;
    }
out:
    return s;                       /* 0+1+2+3+4 */
}

/* 00207's shape: a label after the VLA, reached again by goto */
static int label_after(int n)
{
    char t[n];
    int hits = 0;

    t[0] = 'x';
    if (0)
label:
        hits++;
    if (n-- == 0)
        return hits * 10 + (t[0] == 'x');
    goto label;
}

static int returns_from_vla(int n)
{
    int i;

    for (i = 0; ; i++) {
        int v[n + i];
        v[n + i - 1] = 40 + i;
        if (i == 3)
            return v[n + i - 1];    /* released before the value is read */
    }
}

static int switch_vla(int k, int n)
{
    switch (k) {
    case 1: {
        int v[n];
        v[0] = 5;
        if (v[0])
            break;
        return -1;
    }
    default:
        return -2;
    }
    return 5;
}

static int depth_sum(int depth)
{
    int v[depth + 1];
    int i, below;

    for (i = 0; i <= depth; i++)
        v[i] = depth;
    below = depth ? depth_sum(depth - 1) : 0;
    for (i = 0; i <= depth; i++)
        if (v[i] != depth)
            return -10000;          /* a deeper level overwrote ours */
    return below + depth;
}

/* The callee writes the caller's VLA while it holds one of its own. */
static void fill_from(int *dst, int n, int seed)
{
    int tmp[n];
    int i;

    for (i = 0; i < n; i++)
        tmp[i] = seed + i;
    for (i = 0; i < n; i++)
        dst[i] = tmp[n - 1 - i];
}

static int matrix(int r, int c)
{
    int m[r][c];
    int i, j, s = 0;

    for (i = 0; i < r; i++)
        for (j = 0; j < c; j++)
            m[i][j] = i * 10 + j;
    for (i = 0; i < r; i++)
        s += m[i][c - 1];
    return s == (r * (r - 1) / 2) * 10 + r * (c - 1)
        && (int)sizeof m == r * c * 4 && (int)sizeof m[0] == c * 4;
}

static int sum2d(int r, int c, int m[r][c])
{
    int i, j, s = 0;

    for (i = 0; i < r; i++)
        for (j = 0; j < c; j++)
            s += m[i][j];
    return s;
}

#define vmax(x, y) ({ __typeof__(x) _x = (x); __typeof__(y) _y = (y); \
                      (void)(&_x == &_y); _x > _y ? _x : _y; })

static int stmt_exprs(int n)
{
    /* the kernel's char sym[max(...)]: a VLA whose size is a statement
     * expression */
    char sym[vmax(2 * n + 4, n + 9)];
    int x;

    sym[sizeof sym - 1] = 0;
    /* Each statement expression releases its VLA before the sum is
     * formed, and the second allocates where the first one was. */
    x = ({ int a[n]; a[0] = 7; a[0]; }) + ({ int b[n]; b[0] = 5; b[0]; });
    return x == 12 && (int)sizeof sym == vmax(2 * n + 4, n + 9);
}

struct rec { char tag; double val; };

static int structs(int n)
{
    struct rec r[n];
    int i, ok = 1;

    for (i = 0; i < n; i++) {
        r[i].tag = 'a' + i;
        r[i].val = i * 0.5;
    }
    for (i = 0; i < n; i++)
        ok &= r[i].tag == 'a' + i && r[i].val == i * 0.5;
    return ok && ((unsigned)r & 3) == 0;
}

static void single(void)
{
    char *m0 = __tp_vla_mark();
    int buf[6], i, s;

    check("frame", frame(9, 11, 22, 33, 44, 55));
    check("frame-1", frame(1, 11, 22, 33, 44, 55));
    check("loop", loop_sizes(200) == 15 * 78 + 10);  /* sum of i % 13 */
    check("nested", nested(6));
    check("goto", jumps(3) == 10);
    check("label", label_after(2) == 21);
    check("return", returns_from_vla(2) == 43);
    check("switch", switch_vla(1, 4) == 5);
    check("recursion", depth_sum(12) == 78);
    fill_from(buf, 6, 30);
    for (s = 0, i = 0; i < 6; i++)
        s += buf[i] * (i + 1);
    check("callee", s == 35 + 34 * 2 + 33 * 3 + 32 * 4 + 31 * 5 + 30 * 6);
    {
        int n = 4;
        int vv[n];
        fill_from(vv, n, 1);
        check("caller-vla", vv[0] == 4 && vv[3] == 1);
    }
    check("matrix", matrix(5, 7));
    {
        int r = 3, c = 4, m[r][c], i2, j2;
        for (i2 = 0; i2 < r; i2++)
            for (j2 = 0; j2 < c; j2++)
                m[i2][j2] = i2 + j2;
        check("vla-param", sum2d(r, c, m) == 30);
    }
    check("stmt-expr", stmt_exprs(5));
    check("struct", structs(5));
    check("released", __tp_vla_mark() == m0);
}

/* ------------------------------------------------------------------ */
/* Processes                                                           */
/* ------------------------------------------------------------------ */

#define ROUNDS 40

static Channel ab = NOPROCESS, ba = NOPROCESS;
static Channel done = NOPROCESS, hitick = NOPROCESS, hidone = NOPROCESS;
static Channel ovstart = NOPROCESS;
static int wsa[1024], wsb[1024], wsh[512], wsov[512];
static volatile int ov_survived;

static int verify(const int *v, int n, int seed, int round)
{
    int u[n % 5 + 1];               /* a VLA of the callee's own */
    int i, bad = 0;

    for (i = 0; i <= n % 5; i++)
        u[i] = -1;
    for (i = 0; i < n; i++)
        bad += v[i] != seed * 1000 + round * 7 + i;
    for (i = 0; i <= n % 5; i++)
        bad += u[i] != -1;
    return bad;
}

static int inside(const void *p, const int *ws, int words)
{
    return (const char *)p >= (const char *)ws
        && (const char *)p < (const char *)(ws + words);
}

/* A and B take turns, each holding a VLA while the other allocates and
 * releases its own, the sizes going up and down out of step - one shared
 * arena would hand one of them bytes the other is still using. */
static int pingpong(Channel *out, Channel *in, int seed, int first,
                    const int *ws, int words)
{
    int round, i, tok = 0, bad = 0;

    for (round = 0; round < ROUNDS; round++) {
        int n = 4 + (round * seed) % 29;
        int v[n];

        for (i = 0; i < n; i++)
            v[i] = seed * 1000 + round * 7 + i;
        if (first) {
            ChanOut(out, &tok, 4);
            ChanIn(in, &tok, 4);
        } else {
            ChanIn(in, &tok, 4);
            ChanOut(out, &tok, 4);
        }
        bad += verify(v, n, seed, round);
        bad += !inside(v, ws, words);
    }
    return bad;
}

static void worker_a(void)
{
    int bad = pingpong(&ab, &ba, 3, 1, wsa, 1024);
    ChanOut(&done, &bad, 4);
    PStop();
}

/* B ends by returning, not by PStop. */
static void worker_b(void)
{
    int bad = pingpong(&ba, &ab, 5, 0, wsb, 1024);
    ChanOut(&done, &bad, 4);
}

/* High priority: it holds a VLA while blocked, and the low-priority
 * processes run meanwhile. */
static void worker_h(void)
{
    int round, i, x, bad = 0;

    for (round = 0; round < ROUNDS; round++) {
        int v[3 + round % 11];

        for (i = 0; i < 3 + round % 11; i++)
            v[i] = 7000 + round * 7 + i;
        ChanIn(&hitick, &x, 4);
        bad += verify(v, 3 + round % 11, 7, round);
        bad += !inside(v, wsh, 512);
    }
    ChanOut(&hidone, &bad, 4);
    PStop();
}

/* Asks for 16 KiB in a 2 KiB workspace: must stop before it can use it.
 * (It also sets the error flag, but t4 models the T9000, where an
 * interrupted low-priority process gets its own status back, so the main
 * program cannot see that.) */
static void overflow(void)
{
    int tok = 1, n = 4096;
    ChanOut(&ovstart, &tok, 4);
    {
        int v[n];
        v[0] = 1;
        ov_survived = v[0];
    }
    PStop();
}

/* Then the same workspace again, within its means. */
static void modest(void)
{
    int n = 100, i, bad = 0;
    int v[n];

    for (i = 0; i < n; i++)
        v[i] = i;
    for (i = 0; i < n; i++)
        bad += v[i] != i;
    bad += !inside(v, wsov, 512);
    ChanOut(&ovstart, &bad, 4);
}

static void processes(void)
{
    char *m0 = __tp_vla_mark();
    int round, i, a, b, h, bad = 0, tok;

    PRun(PSetup(wsa, sizeof wsa, worker_a) | PRI_LOW);
    PRun(PSetup(wsb, sizeof wsb, worker_b) | PRI_LOW);
    PRun(PSetup(wsh, sizeof wsh, worker_h) | PRI_HIGH);

    /* The main program uses the main arena meanwhile. */
    for (round = 0; round < ROUNDS; round++) {
        int v[2 + round % 7];

        for (i = 0; i < 2 + round % 7; i++)
            v[i] = 9000 + round * 7 + i;
        ChanOut(&hitick, &round, 4);
        bad += verify(v, 2 + round % 7, 9, round);
        bad += inside(v, wsa, 1024) || inside(v, wsb, 1024)
            || inside(v, wsh, 512);
    }
    ChanIn(&done, &a, 4);
    ChanIn(&done, &b, 4);
    ChanIn(&hidone, &h, 4);
    check("proc-main", bad == 0);
    check("proc-ab", a == 0 && b == 0);
    check("proc-high", h == 0);

    /* Both have stopped, one by PStop and one by returning: their
     * workspaces can be used again. */
    PRun(PSetup(wsa, sizeof wsa, worker_a) | PRI_LOW);
    PRun(PSetup(wsb, sizeof wsb, worker_b) | PRI_LOW);
    ChanIn(&done, &a, 4);
    ChanIn(&done, &b, 4);
    check("proc-reuse", a == 0 && b == 0);

    /* High priority, so it runs until it stops before we continue.  It
     * also leaves the record PSetup made, so the workspace can be reused. */
    PRun(PSetup(wsov, sizeof wsov, overflow) | PRI_HIGH);
    ChanIn(&ovstart, &tok, 4);
    check("overflow-stops", tok == 1 && ov_survived == 0);
    PRun(PSetup(wsov, sizeof wsov, modest) | PRI_HIGH);
    ChanIn(&ovstart, &tok, 4);
    check("overflow-reuse", tok == 0);

    check("proc-released", __tp_vla_mark() == m0);
}

void test_main(void)
{
    single();
    processes();
    uart_puts(pass ? "VLA-OK\n" : "VLA-FAIL\n");
}
