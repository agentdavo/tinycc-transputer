/*
 * test_call_arg_staging.c - argument words staged in the caller's workspace.
 *
 * Register words that must survive the evaluation of later arguments are
 * spilled with `stl s` and read back with `ldl s` before the call; stacked
 * words (ABI word 3+) are stored to Wptr[0..n-1] and read only by the callee.
 * The evaluation-stack scheduler deletes stores it believes nothing reads, so
 * a constant (`ldc k; stl s`) or a plain local (`ldl x; stl s`) staged for a
 * callee must count as read by the call, not as dead.
 *
 * Expected: SSTAGEOK\nH
 */
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?('0'+n):('a'+n-10));}}

static int bad;
static void eq(int where, unsigned got, unsigned want)
{
    if (got != want) {
        bad++;
        us("\nF "); uh(where); us(" "); uh(got); us(" "); uh(want);
    }
}

static unsigned sink[12];

/* Every word checked, so a dropped staging store shows up as a wrong word. */
static void __attribute__((noinline)) take7(unsigned a, unsigned b, unsigned c,
                                            unsigned d, unsigned e, unsigned f,
                                            unsigned g)
{
    sink[0] = a; sink[1] = b; sink[2] = c; sink[3] = d;
    sink[4] = e; sink[5] = f; sink[6] = g;
}

static unsigned __attribute__((noinline)) sum10(unsigned a, unsigned b,
        unsigned c, unsigned d, unsigned e, unsigned f, unsigned g,
        unsigned h, unsigned i, unsigned j)
{
    return a + 2*b + 3*c + 5*d + 7*e + 11*f + 13*g + 17*h + 19*i + 23*j;
}

static unsigned __attribute__((noinline)) id(unsigned x) { return x; }
static unsigned __attribute__((noinline)) mix(unsigned a, unsigned b, unsigned c)
{
    return a * 3 + b * 5 + c * 7;
}

static unsigned __attribute__((noinline)) ret5(unsigned a, unsigned b,
        unsigned c, unsigned d, unsigned e)
{
    return a ^ (b << 1) ^ (c << 2) ^ (d << 3) ^ (e << 4);
}

static void check7(int where, unsigned a, unsigned b, unsigned c, unsigned d,
                   unsigned e, unsigned f, unsigned g)
{
    eq(where + 0, sink[0], a); eq(where + 1, sink[1], b);
    eq(where + 2, sink[2], c); eq(where + 3, sink[3], d);
    eq(where + 4, sink[4], e); eq(where + 5, sink[5], f);
    eq(where + 6, sink[6], g);
}

/* Constants only: every stacked word is `ldc k; stl s`. */
static void consts(void)
{
    take7(1, 2, 3, 4, 5, 6, 7);
    check7(0x100, 1, 2, 3, 4, 5, 6, 7);
}

/* Plain locals: every stacked word is a copy `ldl x; stl s`. */
static void __attribute__((noinline)) locals(unsigned p, unsigned q)
{
    unsigned x = p + 1, y = q + 2, z = p ^ q;
    take7(x, y, z, x, y, z, p);
    check7(0x200, p + 1, q + 2, p ^ q, p + 1, q + 2, p ^ q, p);
}

/* The same slot staged for two calls in a row, with different values. */
static void twice(unsigned p)
{
    take7(p, p, p, 10, 11, 12, 13);
    check7(0x300, p, p, p, 10, 11, 12, 13);
    take7(p, p, p, 20, p, 22, p);
    check7(0x310, p, p, p, 20, p, 22, p);
}

/* Impure register words force the spill/reload path; the stacked words are
 * themselves calls, so their values come back on the stack mid-staging. */
static void impure(unsigned p)
{
    unsigned r = sum10(id(p), mix(p, 1, 2), id(3), p * 4, id(5),
                       mix(6, p, 7), 8, id(p + 9), 10, mix(p, p, p));
    unsigned want = p + 2*(p*3 + 5 + 14) + 3*3 + 5*(p*4) + 7*5 +
                    11*(18 + p*5 + 49) + 13*8 + 17*(p + 9) + 19*10 +
                    23*(p*15);
    eq(0x400, r, want);
}

/* A discarded result, then a loop whose body stages the same slots each
 * time round: liveness must carry the callee's reads across the back edge. */
static void loop(unsigned p)
{
    unsigned i, acc = 0;
    for (i = 0; i < 6; i++) {
        ret5(i, p, i, p, i);
        acc += ret5(i, i + 1, p, 3, i * 2);
    }
    {
        unsigned want = 0;
        for (i = 0; i < 6; i++)
            want += i ^ ((i + 1) << 1) ^ (p << 2) ^ (3 << 3) ^ ((i * 2) << 4);
        eq(0x500, acc, want);
    }
}

/* Indirect calls stage the same way before the trampoline. */
static void indirect(unsigned p)
{
    void (*volatile fp)(unsigned, unsigned, unsigned, unsigned, unsigned,
                        unsigned, unsigned) = take7;
    fp(p, 2, p + 3, 4, 5, p, 7);
    check7(0x600, p, 2, p + 3, 4, 5, p, 7);
}

void test_main(void)
{
    volatile unsigned seed = 0x1234;
    consts();
    locals(seed, seed * 3);
    twice(seed + 1);
    impure(seed);
    loop(seed);
    indirect(seed);
    us(bad ? "\nSTAGEFAIL" : "STAGEOK\n");
}
