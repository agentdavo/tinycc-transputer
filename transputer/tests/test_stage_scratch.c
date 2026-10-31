/*
 * test_stage_scratch.c - call words staged in workspace slots must survive
 * the halfword loads that go through the backend scratch slots.
 * Expected: SSTAGEOK\nH
 *
 * tp_stage_call_words spills the register words of a call with stacked
 * arguments to the slots just above the highest fixed slot the function
 * has used *so far*.  When the function's one earlier scratch use is a
 * two-sb 16-bit store (slot 15, or 15-16 with the value in AReg), those
 * spills land on 16-18 or 17-19.  A variadic halfword argument is still an
 * lvalue when the call is staged, and loading it - two byte loads parked
 * in slots 15-16 for a local, 18-19 through a pointer or for a global -
 * overwrote the spills: c-testsuite 09003 printed a pointer for its second
 * argument.  Each shape below is its own function so that store is the
 * only thing ahead of the call.  The halfwords sit at odd offsets, so
 * -mt9000 takes the same paths (ss/ls need an even address).
 */
#include <stdarg.h>

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void ux(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        up("0123456789abcdef"[(v >> i) & 15]);
}

struct __attribute__((packed)) ph {
    unsigned char pad;
    unsigned short a, b, c, d;
};

static struct ph G, P;
static int got[24], ngot, bad;

/* Record the call: its tag, then n more words. */
static void gotv(int tag, int n, ...)
{
    va_list ap;
    int i;

    got[0] = tag;
    va_start(ap, n);
    for (i = 1; i <= n && i < 24; i++)
        got[i] = va_arg(ap, int);
    va_end(ap);
    ngot = n + 1;
}

static void want(int n, const int *w)
{
    int i;

    if (ngot != n) {
        bad++;
        us("\nFAIL "); ux(w[0]); us(" words "); ux(ngot);
        return;
    }
    for (i = 0; i < n; i++) {
        if (got[i] != w[i]) {
            bad++;
            us("\nFAIL "); ux(w[0]); us(" word "); ux(i);
            us(" got "); ux(got[i]); us(" want "); ux(w[i]);
        }
    }
}

/* halfwords through a pointer: scratch 18-19 */
static void s_ptr(struct ph *p)
{
    p->a = 0x1111;
    gotv(1, 4, p->a, p->b, p->c, p->d);
}

/* the same with the stored value computed into AReg first */
static void s_ptr_areg(struct ph *p, int v)
{
    p->a = v * 3 + 0x1000;
    gotv(2, 4, p->a, p->b, p->c, p->d);
}

/* halfwords of a global: scratch 18-19 (c-testsuite 09003's shape) */
static void s_global(void)
{
    G.a = 0x1111;
    gotv(3, 4, G.a, G.b, G.c, G.d);
}

/* halfwords of a local: scratch 15-16 */
static void s_local(int v)
{
    struct ph l;

    l.d = v + 3;
    l.c = v + 2;
    l.b = v + 1;
    l.a = v;
    gotv(4, 4, l.a, l.b, l.c, l.d);
}

/* an indirect call, whose target is parked in a scratch slot too */
static void s_indirect(struct ph *p, void (*fn)(int, int, ...))
{
    p->a = 0x1111;
    fn(5, 4, p->a, p->b, p->c, p->d);
}

/* no stacked words: the register words spill to the low slots */
static void s_three(struct ph *p)
{
    p->a = 0x1111;
    gotv(6, 1, p->a);
}

/* 21 stacked words: the staging area itself covers the scratch slots */
static void s_wide(struct ph *p)
{
    p->a = 0x1111;
    gotv(7, 22, p->a, p->b, p->c, p->d, p->a, p->b, p->c, p->d,
         p->a, p->b, p->c, p->d, p->a, p->b, p->c, p->d,
         p->a, p->b, p->c, p->d, p->a, p->b);
}

static void reset(struct ph *p)
{
    p->a = 0; p->b = 0x2222; p->c = 0x3333; p->d = 0x4444;
    ngot = 0;
}

void test_main(void)
{
    static const int w1[5] = { 1, 0x1111, 0x2222, 0x3333, 0x4444 };
    static const int w2[5] = { 2, 0x1495, 0x2222, 0x3333, 0x4444 };
    static const int w3[5] = { 3, 0x1111, 0x2222, 0x3333, 0x4444 };
    static const int w4[5] = { 4, 0x7770, 0x7771, 0x7772, 0x7773 };
    static const int w5[5] = { 5, 0x1111, 0x2222, 0x3333, 0x4444 };
    static const int w6[2] = { 6, 0x1111 };
    int w7[23], i;

    reset(&P); s_ptr(&P); want(5, w1);
    reset(&P); s_ptr_areg(&P, 0x187); want(5, w2);
    reset(&G); s_global(); want(5, w3);
    ngot = 0; s_local(0x7770); want(5, w4);
    reset(&P); s_indirect(&P, gotv); want(5, w5);
    reset(&P); s_three(&P); want(2, w6);
    reset(&P); s_wide(&P);
    w7[0] = 7;
    for (i = 1; i < 23; i++)
        w7[i] = 0x1111 * (1 + (i - 1) % 4);
    want(23, w7);

    us(bad ? "\nSTAGEFAIL\n" : "STAGEOK\n");
}
