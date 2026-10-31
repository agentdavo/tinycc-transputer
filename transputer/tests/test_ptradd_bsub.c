/*
 * test_ptradd_bsub.c - value-correctness for the BSUB pointer-add lowering.
 *
 * The final `+` of a pointer computation is emitted as `bsub` rather than
 * `sum`.  The two compute the same thing (AReg + BReg, commutative); the
 * point is that the T9000 grouper charges `bsub` to stage 2 (address
 * calculations, 2 per cycle, measurably spare) and `sum` to stage 4 (ALU,
 * 1 per cycle, the narrowest resource in the machine).
 *
 * Since it is a semantics-free substitution, what this checks is that it
 * really is semantics-free, across every element size and addressing form:
 * byte / 16-bit / word / double / long long elements, struct strides that
 * are and are not powers of two, arrays of pointers, negative subscripts,
 * pointer difference (which must stay `diff`, not `bsub`) and `+=` / `-=`.
 *
 * A/B the lowering with TCC_TRANSPUTER_BSUB_OPT=0.
 *
 * Deliberately split into several small functions: one combined function
 * large enough to hold all of it trips the pre-existing large-function
 * miscompilation (it hangs with the lowering disabled too), which would
 * otherwise look like a failure of this feature.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void ud(unsigned v){char b[12];int n=0;if(!v){up('0');return;}while(v){b[n++]=(char)('0'+v%10);v/=10;}while(n)up(b[--n]);}

static int bad = 0, ck = 0;
static void eq(const char *w, int i, long g, long e)
{
    ck++;
    if (g != e) {
        bad++;
        us("\nFAIL "); us(w); us("["); ud((unsigned)i);
        us("] got="); ud((unsigned)g); us(" want="); ud((unsigned)e);
    }
}

struct pt { int a, b, c, d; };      /* 16-byte stride, power of two   */
struct s3 { char x, y, z; };        /* 3-byte stride, not a power     */
struct s5 { char p[5]; };           /* 5-byte stride, nested subscript */

static char       C[64];
static short      S[64];
static int        I[64];
static double     D[16];
static long long  L[16];
static struct pt  P[16];
static struct s3  T[16];
static struct s5  F[16];
static int       *ptrs[8];

static void t_scalar(void)
{
    int i;
    for (i = 0; i < 64; i++) { C[i] = (char)(i+1); S[i] = (short)(i*3); I[i] = i*5; }
    for (i = 0; i < 64; i++) {
        eq("char",  i, C[i], i+1);
        eq("short", i, S[i], i*3);
        eq("int",   i, I[i], i*5);
    }
}

static void t_wide(void)
{
    int i;
    for (i = 0; i < 16; i++) { D[i] = (double)i * 0.5; L[i] = (long long)i * 3LL; }
    for (i = 0; i < 16; i++) {
        eq("double",   i, (long)(D[i] * 2.0), i);
        eq("longlong", i, (long)L[i], i*3);
    }
}

static void t_struct(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        P[i].a = i;          P[i].c = i+2;
        T[i].x = (char)i;    T[i].y = (char)(i+10);
        F[i].p[0] = (char)i; F[i].p[4] = (char)(i+7);
    }
    for (i = 0; i < 16; i++) {
        eq("pt.a", i, P[i].a, i);            /* 16-byte stride  */
        eq("pt.c", i, P[i].c, i+2);
        eq("s3.x", i, T[i].x, i);            /* 3-byte stride   */
        eq("s3.y", i, T[i].y, i+10);
        eq("s5.0", i, F[i].p[0], i);         /* 5-byte + nested */
        eq("s5.4", i, F[i].p[4], i+7);
    }
}

static void t_ptrarray(void)
{
    int i;
    for (i = 0; i < 8; i++) ptrs[i] = &I[i*2];
    for (i = 0; i < 8; i++) eq("ptrptr", i, *ptrs[i], i*2*5);
}

static void t_walk(void)
{
    int i;
    { char *p = C + 32;      eq("neg.char",  0, p[-1], 32);
                             eq("neg.char",  1, *(p-32), 1); }
    { short *p = S + 32;     eq("neg.short", 0, p[-2], (32-2)*3); }
    { int *p = I + 32;       eq("neg.int",   0, p[-4], (32-4)*5);
                             eq("ptrdiff",   0, p - I, 32); }
    { struct pt *p = P + 8;  eq("neg.pt",    0, p[-3].c, 5+2); }
    { char *a = C, *b = C + 10; eq("chardiff", 0, b - a, 10); }
    { int j = 3; char *p = C; p += j; eq("pluseq",  0, *p, 4);
                                 p -= j; eq("minuseq", 0, *p, 1); }
    { short s2[8]; short *q; for (i = 0; i < 8; i++) s2[i] = (short)(i*3);
      q = s2 + 4; eq("local.short", 0, q[-2], (4-2)*3); }
}

int test_main(void)
{
    t_scalar();
    t_wide();
    t_struct();
    t_ptrarray();
    t_walk();

    us("\nchecks="); ud((unsigned)ck); us(" failures="); ud((unsigned)bad);
    us(bad ? "\nPTRADD: FAIL\n" : "\nPTRADD: PASS\n");
    return bad ? 1 : 0;
}
