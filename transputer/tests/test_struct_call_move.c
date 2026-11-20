/* By-value aggregate argument copies, including partial ABI words and
 * arguments after multiple/nested structures. Expected: SSTRUCTMOVE OK\nH */
#define UART (*(volatile unsigned int *)0x10000000u)
static void put(const char *s) { while (*s) UART = (unsigned char)*s++; }
static volatile unsigned base = 17;
static unsigned errors;

#define CASE(N) \
struct s##N { unsigned char b[N]; }; \
static unsigned __attribute__((noinline)) take##N(struct s##N v, unsigned t) \
{ unsigned s = t, i; for (i = 0; i < N; i++) s += v.b[i] * (i + 1); return s; } \
static void check##N(unsigned x) \
{ struct s##N v; unsigned i, s = 123; \
  for (i = 0; i < N; i++) { v.b[i] = (unsigned char)(x + i * 3); s += v.b[i] * (i + 1); } \
  if (take##N(v, 123) != s) errors++; }

CASE(0) CASE(1) CASE(2) CASE(3) CASE(4) CASE(5) CASE(6) CASE(7)
CASE(8) CASE(9) CASE(10) CASE(11) CASE(12) CASE(13) CASE(14) CASE(15)
CASE(16) CASE(17) CASE(65)

static volatile struct s9 volatile_value;
struct volatile_inner { volatile unsigned value; };
struct volatile_outer { unsigned char lead; struct volatile_inner item[2]; unsigned char tail; };
static struct volatile_outer volatile_members;
static unsigned __attribute__((noinline)) take_volatile_members(struct volatile_outer a)
{ return a.lead + a.item[0].value + a.item[1].value + a.tail; }
static struct s3 small_value = {{ 4, 5, 6 }};
static struct s1 single_value = {{ 4 }};
static int __attribute__((noinline)) check_char_guard(void)
{
    volatile unsigned char guard = 0xa5;
    if (take1(single_value, 7) != 11) return 0;
    return guard == 0xa5;
}
static int __attribute__((noinline)) check_short_guard(void)
{
    volatile unsigned short guard = 0xa55a;
    if (take3(small_value, 7) != 39) return 0;
    return guard == 0xa55a;
}

struct __attribute__((packed)) mixed { unsigned char c; unsigned n; unsigned short s; };
struct outer { struct s9 a; struct mixed b; struct s17 c; };
static struct outer __attribute__((noinline)) combine(struct s9 a, struct mixed b,
                                                      struct s17 c, unsigned end)
{
    struct outer r;
    r.a = a; r.b = b; r.c = c; r.c.b[16] ^= end;
    return r;
}
static unsigned __attribute__((noinline)) inspect(struct outer r, struct s9 a,
                                                   unsigned end)
{
    return r.a.b[4] + r.b.c + r.b.n + r.b.s + r.c.b[16] + a.b[8] + end;
}
void test_main(void)
{
    struct s9 a; struct s17 c; struct mixed b; struct outer r;
    unsigned i, x = base;
    check0(x); check1(x); check2(x); check3(x); check4(x); check5(x);
    check6(x); check7(x); check8(x); check9(x); check10(x); check11(x);
    check12(x); check13(x); check14(x); check15(x); check16(x); check17(x); check65(x);
    if (!check_char_guard() || !check_short_guard()) errors++;
    for (i = 0; i < 9; i++) volatile_value.b[i] = x + i;
    {
        volatile unsigned char guard = 0xa5;
        if (take9(volatile_value, 123) != 123 + 45 * x + 240) errors++;
        if (guard != 0xa5) errors++;
    }
    {
        volatile struct s9 *p = &volatile_value;
        if (take9(*p, 123) != 123 + 45 * x + 240) errors++;
    }
    volatile_members.lead = 1; volatile_members.item[0].value = 2;
    volatile_members.item[1].value = 3; volatile_members.tail = 4;
    if (take_volatile_members(volatile_members) != 10) errors++;
    for (i = 0; i < 9; i++) a.b[i] = x + i;
    for (i = 0; i < 17; i++) c.b[i] = x + i * 2;
    b.c = 3; b.n = 47; b.s = 31;
    r = combine(a, b, c, 5);
    if (inspect(r, a, 9) != (x+4) + 3 + 47 + 31 + ((x+32)^5) + (x+8) + 9)
        errors++;
    put(errors ? "STRUCTMOVE FAIL\n" : "STRUCTMOVE OK\n");
}
