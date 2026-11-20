/* Expected: SACCUMOK\nH */
/* Flags: -mstrict-aliasing */
/*
 * test_accum_promote.c - -mstrict-aliasing (tp_accum_promote,
 * transputer-fnopt.h): a loop's `*p op= v` kept in a frame word, loaded
 * before the loop and stored after it.
 *
 *   mm, mv     CoreMark's matrix products (int accumulators, short inputs):
 *              promoted; N = 0 runs the loops no times
 *   bytes      the inputs are char: they may read the accumulator, so the
 *              loop keeps its memory accesses (and sees the partial sums)
 *   called     a call in the loop: not promoted
 *   words      an int input beside the int accumulator: not promoted
 *   moving     the accumulator's pointer moves in the loop: not promoted
 *
 * Each result is compared with the same sum over indices into volatile
 * copies, which no pass can rearrange.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }

static void mm(unsigned N, int *C, short *A, short *B)
{
    unsigned i, j, k;
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++) {
            C[i * N + j] = 0;
            for (k = 0; k < N; k++)
                C[i * N + j] += A[i * N + k] * B[k * N + j];
        }
}

static void mv(unsigned N, int *C, short *A, short *B)
{
    unsigned i, j;
    for (i = 0; i < N; i++) {
        C[i] = 0;
        for (j = 0; j < N; j++)
            C[i] += A[i * N + j] * B[j];
    }
}

/* char inputs may alias the accumulator: here they do, overlapping C */
static void bytes(int n, int *acc, unsigned char *p)
{
    int k;
    for (k = 0; k < n; k++)
        *acc += p[k];
}

static int calls;
static void tick(void) { calls++; }
static void called(int n, int *acc, short *p)
{
    int k;
    for (k = 0; k < n; k++) {
        *acc += p[k];
        tick();
    }
}

/* an int input, which may be the accumulator itself */
static void words(int n, int *acc, int *p)
{
    int k;
    for (k = 0; k < n; k++)
        *acc += p[k];
}

static void moving(int n, int *acc, short *p)
{
    int k;
    for (k = 0; k < n; k++) {
        *acc += p[k];
        acc++;
    }
}

static unsigned seed = 77;
static unsigned rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 16;
}

int test_main(void)
{
    static short A[64], B[64];
    static int C[80];
    static volatile short VA[64], VB[64];
    int bad = 0, i, j, k, N;

    for (N = 0; N <= 8; N++) {
        for (i = 0; i < N * N; i++) {
            A[i] = VA[i] = (short)(rnd() - 32768);
            B[i] = VB[i] = (short)(rnd() - 32768);
        }
        for (i = 0; i < 80; i++)
            C[i] = 0x5a5a;
        mm((unsigned)N, C, A, B);
        for (i = 0; i < N; i++)
            for (j = 0; j < N; j++) {
                int s = 0;
                for (k = 0; k < N; k++)
                    s += VA[i * N + k] * VB[k * N + j];
                if (C[i * N + j] != s)
                    bad++;
            }
        if (C[N * N] != 0x5a5a)
            bad++;
        mv((unsigned)N, C, A, B);
        for (i = 0; i < N; i++) {
            int s = 0;
            for (j = 0; j < N; j++)
                s += VA[i * N + j] * VB[j];
            if (C[i] != s)
                bad++;
        }
    }
    {
        /* acc is C[0]; the bytes read run over C[0] itself: each step sees
         * the sum so far.  Little endian: byte 0 is the low byte. */
        volatile int ref = 3;
        int t, step;
        C[0] = 3; C[1] = 0x01020304;
        bytes(8, &C[0], (unsigned char *)&C[0]);
        for (step = 0; step < 8; step++) {
            unsigned char b;
            t = ref;
            if (step < 4)
                b = (unsigned char)((unsigned)t >> (8 * step));
            else
                b = (unsigned char)(0x01020304u >> (8 * (step - 4)));
            ref = t + b;
        }
        if (C[0] != ref)
            bad++;
    }
    {
        short p[5] = { 1, 2, 3, 4, 5 };
        int acc = 10;
        calls = 0;
        called(5, &acc, p);
        if (acc != 25 || calls != 5)
            bad++;
    }
    {
        int w[4] = { 1, 2, 3, 4 };
        words(4, &w[1], w);         /* w[1] read after it is added to */
        /* k=0: w1 = 2+1 = 3; k=1: w1 = 3+3 = 6; k=2: 6+3 = 9; k=3: 9+4 = 13 */
        if (w[1] != 13)
            bad++;
    }
    {
        short p[3] = { 7, 8, 9 };
        int q[3] = { 1, 1, 1 };
        moving(3, q, p);
        if (q[0] != 8 || q[1] != 9 || q[2] != 10)
            bad++;
    }
    us(bad ? "ACCUMFAIL\n" : "ACCUMOK\n");
    return 0;
}
