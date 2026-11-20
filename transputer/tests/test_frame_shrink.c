/*
 * test_frame_shrink.c - frame words nothing names are dropped and what sits
 * above them (the post-pass's hoisted and cached words, the return address,
 * the parameters) moves down with the AJWs (tp_slot_remap, frame shrink).
 * Functions here use many temporaries during code generation that the
 * scheduler then deletes, so their frames had dead words.  Checked: reads
 * and writes of parameters, the address of a parameter written through, a
 * double parameter, a local array walked to one past its end, loop
 * invariants hoisted next to five parameters (CoreMark's matrix shape),
 * recursion, several returns, and a call made from the shrunk frame.  One
 * letter per check: upper case passes, '!' fails.
 *
 * Expected: SABCDEFGH\nH
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static void check(int ok, int letter)
{
    uart_putc(ok ? letter : '!');
}

typedef short MATDAT;
typedef int MATRES;

/* CoreMark's matrix_mul_matrix: the bounds and strides are invariants */
static void mul(unsigned N, MATRES *C, const MATDAT *A, const MATDAT *B,
                int bias)
{
    unsigned i, j, k;
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++) {
            C[i * N + j] = bias;
            for (k = 0; k < N; k++)
                C[i * N + j] += (MATRES)A[i * N + k] * (MATRES)B[k * N + j];
        }
}

static void bump(int *p, int by)
{
    *p += by;
}

/* a parameter whose address is taken and written through */
static int through_addr(int a, int b, int c, int d)
{
    int *p = &c;
    bump(p, a * b);
    *(&d) += c;
    return c * 1000 + d;
}

static double scale(double x, int n, double y)
{
    int i;
    double s = 0;
    for (i = 0; i < n; i++)
        s = s + x * y;
    return s;
}

static int walk(int seed)
{
    int a[12], *p, *end = a + 12, sum = 0, i;
    for (i = 0; i < 12; i++)
        a[i] = seed + i * i;
    for (p = a; p != end; p++)
        sum += *p;
    return sum;
}

static int fib(int n, int depth, int unused1, int unused2)
{
    int x, y;
    if (n < 2)
        return n + unused1 - unused2;
    x = fib(n - 1, depth + 1, unused1, unused2);
    y = fib(n - 2, depth + 1, unused1, unused2);
    return x + y;
}

static int returns(int k, int a, int b, int c, int d, int e)
{
    int t = a * b + c;
    if (k == 0)
        return t;
    if (k == 1)
        return t - d;
    t = t * e + d;
    if (k == 2)
        return t;
    return t + k * a * b * c * d * e;
}

void test_main(void)
{
    static MATDAT A[25], B[25];
    static MATRES C[25];
    int i, ok;

    for (i = 0; i < 25; i++) {
        A[i] = (MATDAT)(i - 7);
        B[i] = (MATDAT)(3 * i + 1);
    }
    mul(5, C, A, B, 11);
    ok = 1;
    for (i = 0; i < 25; i++) {
        int r = i / 5, c = i % 5, k, want = 11;
        for (k = 0; k < 5; k++)
            want += (r * 5 + k - 7) * (3 * (k * 5 + c) + 1);
        if (C[i] != want)
            ok = 0;
    }
    check(ok, 'A');
    check(through_addr(3, 4, 5, 6) == 17 * 1000 + 23, 'B');
    check(scale(1.5, 4, 2.0) == 12.0, 'C');
    check(walk(2) == 12 * 2 + 506, 'D');
    check(fib(15, 0, 3, 3) == 610, 'E');
    check(returns(0, 2, 3, 4, 5, 6) == 10 && returns(1, 2, 3, 4, 5, 6) == 5,
          'F');
    check(returns(2, 2, 3, 4, 5, 6) == 65 &&
          returns(3, 2, 3, 4, 5, 6) == 65 + 3 * 720, 'G');
    mul(1, C, A, B, -1);
    check(C[0] == -1 + (-7) * 1, 'H');
    uart_putc('\n');
}
