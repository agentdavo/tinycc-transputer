/* Expected: SDEREFOK\nH */
/*
 * test_deref_local_ptr.c - *p through a pointer held in a local.
 *
 * indir() now leaves *p, for a pointer local that is not volatile and a
 * word or narrower integer or pointer pointee, as an lvalue through p's
 * own word (VT_LLOCAL): the pointer is read at the access instead of being
 * parked in a register across the other operand.  Checked: stores and
 * loads of char, short, int and pointer, signed and unsigned; compound
 * assignment and ++/-- through *p (the pointer read twice); a right side
 * that calls a function; pointers that are parameters; and the cases left
 * on the old path - a volatile pointer, a function pointer, a pointer to
 * an array, a struct, a long long and a double.
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
static void chk(int line, int got, int want)
{
    if (got != want) {
        bad++;
        us("\nFAIL line ");
        uh((unsigned)line);
        us(" got ");
        uh((unsigned)got);
        us(" want ");
        uh((unsigned)want);
    }
}

static int calls;
static int seven(void) { calls++; return 7; }
static int twice(int x) { return 2 * x; }
struct st { int a; short b; };

static void fill(char *c, short *s, int *i, int **pp, int n)
{
    int k;
    for (k = 0; k < n; k++) {
        *c = (char)(k - 3);
        *s = (short)(k * 1000 - 2000);
        *i = k * 100000;
        *pp = i;
        c++; s++; i++; pp++;
    }
}

void test_main(void)
{
    char cb[4]; unsigned char ub[4];
    short sb[4]; unsigned short usb[4];
    int ib[4], *ipb[4];
    char *cp = cb; unsigned char *up_ = ub;
    short *sp = sb; unsigned short *usp = usb;
    int *ip = ib, **pp = ipb, x;
    int * volatile vp = ib;
    int (*fp)(int) = twice;
    int (*pa)[4] = &ib;
    struct st s, *stp = &s;
    long long ll, *llp = &ll;
    double d, *dp = &d;

    *cp = -5;            chk(__LINE__, cb[0], -5);
    *up_ = 250;          chk(__LINE__, ub[0], 250);
    *sp = -30000;        chk(__LINE__, sb[0], -30000);
    *usp = 60000;        chk(__LINE__, usb[0], 60000);
    *ip = 123456;        chk(__LINE__, ib[0], 123456);
    *pp = ip;            chk(__LINE__, ipb[0] == ib, 1);
    chk(__LINE__, *cp + *up_, -5 + 250);
    chk(__LINE__, *sp + *usp, -30000 + 60000);
    chk(__LINE__, **pp, 123456);

    *ip += 4;            chk(__LINE__, ib[0], 123460);
    *cp -= 3;            chk(__LINE__, cb[0], -8);
    (*sp)++;             chk(__LINE__, sb[0], -29999);
    x = (*usp)--;        chk(__LINE__, x, 60000); chk(__LINE__, usb[0], 59999);
    *ip = *ip * 2 + *cp; chk(__LINE__, ib[0], 2 * 123460 - 8);

    calls = 0;
    *ip = seven() + *sp; chk(__LINE__, ib[0], 7 - 29999); chk(__LINE__, calls, 1);
    *ip = twice(*ip);    chk(__LINE__, ib[0], 2 * (7 - 29999));

    fill(cb, sb, ib, ipb, 4);
    chk(__LINE__, cb[3], 0); chk(__LINE__, sb[2], 0); chk(__LINE__, ib[3], 300000);
    chk(__LINE__, ipb[3] == &ib[3], 1);

    /* left on the old path */
    *vp = 11;            chk(__LINE__, ib[0], 11);
    *vp += 1;            chk(__LINE__, ib[0], 12);
    chk(__LINE__, (*fp)(21), 42);
    chk(__LINE__, (*pa)[2], 200000);
    (*stp).a = 5; (*stp).b = -6;
    chk(__LINE__, s.a + s.b, -1);
    *llp = 0x123456789LL; chk(__LINE__, (int)(ll >> 32), 1); chk(__LINE__, (int)ll, 0x23456789);
    *dp = 2.5;           chk(__LINE__, (int)(d * 2), 5);

    us(bad ? "\nDEREFFAIL\n" : "DEREFOK\n");
}
