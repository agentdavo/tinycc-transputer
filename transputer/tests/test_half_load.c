/* Expected: SHALFLOADOK\nH */
/*
 * test_half_load.c - 16-bit loads on the parts without ls.
 *
 * A halfword local at an even frame offset is now read as its workspace
 * word and the half taken out (ldl; ldc #ffff; and, or ldl; ldc 16; shr),
 * a packed odd one from its bytes high first, and one through a pointer
 * from two lb with only the address parked.  Each is checked, signed and
 * unsigned, for values with the sign bit set and neighbours whose bytes
 * must not leak in: locals two to a word, a packed struct, arrays through
 * pointers and globals, as operands of an expression and as call
 * arguments.  Run at -mt800 and -mt414 too (tprun.sh ... -mt800).
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

struct pair { short a; short b; };
struct upair { unsigned short a; unsigned short b; };
#pragma pack(1)
struct packed { char c; short s; unsigned short u; char d; };
#pragma pack()

static short gs[6] = { -1, 0x7fff, -32768, 0x1234, -2, 0 };
static unsigned short gu[6] = { 0xffff, 0x7fff, 0x8000, 0x1234, 0xfffe, 0 };
static volatile int vk = 3;

static int add3(int a, int b, int c) { return a + b + c; }
static int sum_s(const short *p, int n) { int s = 0, i; for (i = 0; i < n; i++) s += p[i]; return s; }
static unsigned sum_u(const unsigned short *p, int n) { unsigned s = 0; int i; for (i = 0; i < n; i++) s += p[i]; return s; }

void test_main(void)
{
    short s0 = -3, s1 = 0x4321;          /* two to a word, maybe */
    unsigned short u0 = 0xfedc, u1 = 0x8001;
    struct pair p;
    struct upair q;
    struct packed k;
    short ls[4];
    unsigned short lu[4];
    char pad[3];
    int i;

    p.a = -32768; p.b = 0x7ffe;
    q.a = 0xffff; q.b = 0x0102;
    k.c = 0x55; k.s = -1234; k.u = 0xbeef; k.d = 0x66;
    ls[0] = -5; ls[1] = 7; ls[2] = -32767; ls[3] = 0x0f0f;
    lu[0] = 0xffff; lu[1] = 1; lu[2] = 0x8000; lu[3] = 0xabcd;
    pad[0] = pad[1] = pad[2] = 0x7f;

    chk(__LINE__, s0, -3);
    chk(__LINE__, s1, 0x4321);
    chk(__LINE__, u0, 0xfedc);
    chk(__LINE__, u1, 0x8001);
    chk(__LINE__, p.a, -32768);
    chk(__LINE__, p.b, 0x7ffe);
    chk(__LINE__, q.a, 0xffff);
    chk(__LINE__, q.b, 0x0102);
    chk(__LINE__, k.s, -1234);
    chk(__LINE__, k.u, 0xbeef);
    chk(__LINE__, k.c + k.d, 0x55 + 0x66);
    for (i = 0; i < 4; i++) {
        static const int ws[4] = { -5, 7, -32767, 0x0f0f };
        static const int wu[4] = { 0xffff, 1, 0x8000, 0xabcd };
        chk(__LINE__, ls[i], ws[i]);
        chk(__LINE__, lu[i], wu[i]);
    }
    chk(__LINE__, ls[1] + ls[2], 7 - 32767);
    chk(__LINE__, lu[2] - lu[1], 0x7fff);
    /* operands of one expression, and call arguments */
    chk(__LINE__, s0 * s1 + u0, -3 * 0x4321 + 0xfedc);
    chk(__LINE__, add3(s0, u1, p.a), -3 + 0x8001 - 32768);
    chk(__LINE__, add3(k.s, q.a, k.u), -1234 + 0xffff + 0xbeef);
    /* through pointers, globals, a variable index */
    chk(__LINE__, sum_s(gs, 6), -1 + 0x7fff - 32768 + 0x1234 - 2);
    chk(__LINE__, (int)sum_u(gu, 6), 0xffff + 0x7fff + 0x8000 + 0x1234 + 0xfffe);
    chk(__LINE__, gs[vk], 0x1234);
    chk(__LINE__, gu[vk - 1], 0x8000);
    chk(__LINE__, sum_s(ls, 4), -5 + 7 - 32767 + 0x0f0f);
    chk(__LINE__, (int)sum_u(&q.a, 2), 0xffff + 0x0102);
    chk(__LINE__, pad[0] + pad[2], 0xfe);
    us(bad ? "\nHALFLOADFAIL\n" : "HALFLOADOK\n");
}
