/*
 * test_switch_table.c - dense switches through a jump table (-mjump-tables).
 *
 * Expected: SSWTABOK\nH
 *
 * The runner compiles this with -mjump-tables; without it the same checks
 * run on compare chains.  Each switch has a twin written with ifs, and the
 * two must agree over a range of selectors that runs well outside the
 * cases on both sides.
 *
 * The table is `ldl idx; ldc 2; ldpi; wsub; gcall` and a four-byte `j` for
 * each index.  A function with one is still relaxed and scheduled, which is
 * what goes wrong if the passes do not know where a dispatch can land:
 *  - live_across: a value stored before the switch and read in one case
 *    only (a store that looks dead if the dispatch has one successor);
 *  - interp: a switch in a loop with values carried round it, the shape of
 *    an interpreter;
 *  - two tables in one function, a switch inside a case, fallthrough,
 *    `continue` and `return` out of cases;
 *  - bases: zero, just above zero (indexed by the value), far from zero,
 *    negative, the ends of int; char, short and unsigned selectors; holes
 *    and case ranges; with and without a default;
 *  - swur: `default: __builtin_unreachable();` drops the range test;
 *  - big: a function too long for four-byte entries, which keeps the
 *    multiply (`ldc 5; prod`).
 */
#define UART (*(volatile unsigned int *)0x10000000u)
static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }

static int bad;
static void check(int got, int want) { if (got != want) bad++; }

/* ---- base 0, default ---- */
int sw0(int x)
{
    switch (x) {
    case 0: return 11;
    case 1: return 22;
    case 2: return 33;
    case 3: return 44;
    case 4: return 55;
    case 5: return 66;
    case 6: return 77;
    case 7: return 88;
    case 8: return 99;
    default: return -1;
    }
}
int if0(int x)
{
    if (x >= 0 && x <= 8) return (x + 1) * 11;
    return -1;
}

/* ---- base just above zero, holes, no default ---- */
int sw3(int x)
{
    int r = 1000;
    switch (x) {
    case 3: r = 1; break;
    case 4: r = 2; break;
    case 6: r = 3; break;
    case 7: r = 4; break;
    case 9: r = 5; break;
    case 10: r = 6; break;
    case 11: r = 7; break;
    case 12: r = 8; break;
    }
    return r + x;
}
int if3(int x)
{
    int r = 1000;
    if (x == 3) r = 1; else if (x == 4) r = 2; else if (x == 6) r = 3;
    else if (x == 7) r = 4; else if (x == 9) r = 5; else if (x == 10) r = 6;
    else if (x == 11) r = 7; else if (x == 12) r = 8;
    return r + x;
}

/* ---- far from zero, case ranges ---- */
int sw1000(int x)
{
    switch (x) {
    case 1000: return 1;
    case 1001 ... 1004: return 2;
    case 1005: return 3;
    case 1007: return 4;
    case 1008 ... 1010: return 5;
    case 1012: return 6;
    case 1013: return 7;
    default: return 0;
    }
}
int if1000(int x)
{
    if (x == 1000) return 1;
    if (x >= 1001 && x <= 1004) return 2;
    if (x == 1005) return 3;
    if (x == 1007) return 4;
    if (x >= 1008 && x <= 1010) return 5;
    if (x == 1012) return 6;
    if (x == 1013) return 7;
    return 0;
}

/* ---- negative base ---- */
int swneg(int x)
{
    switch (x) {
    case -5: return 50;
    case -4: return 40;
    case -3: return 30;
    case -2: return 20;
    case -1: return 10;
    case 0: return 1;
    case 1: return 2;
    case 2: return 3;
    case 3: return 4;
    default: return 99;
    }
}
int ifneg(int x)
{
    if (x >= -5 && x <= -1) return -x * 10;
    if (x >= 0 && x <= 3) return x + 1;
    return 99;
}

/* ---- the ends of int ---- */
int swmax(int x)
{
    switch (x) {
    case 0x7ffffff8: return 1; case 0x7ffffff9: return 2;
    case 0x7ffffffa: return 3; case 0x7ffffffb: return 4;
    case 0x7ffffffc: return 5; case 0x7ffffffd: return 6;
    case 0x7ffffffe: return 7; case 0x7fffffff: return 8;
    default: return 0;
    }
}
int swmin(int x)
{
    switch (x) {
    case -0x7fffffff - 1: return 1; case -0x7fffffff: return 2;
    case -0x7ffffffe: return 3; case -0x7ffffffd: return 4;
    case -0x7ffffffc: return 5; case -0x7ffffffb: return 6;
    case -0x7ffffffa: return 7; case -0x7ffffff9: return 8;
    default: return 0;
    }
}

/* ---- unsigned, above the sign bit ---- */
int swu(unsigned x)
{
    switch (x) {
    case 0x80000000u: return 1; case 0x80000001u: return 2;
    case 0x80000002u: return 3; case 0x80000003u: return 4;
    case 0x80000004u: return 5; case 0x80000005u: return 6;
    case 0x80000006u: return 7; case 0x80000007u: return 8;
    default: return 0;
    }
}

/* ---- narrow selectors ---- */
int swc(signed char x)
{
    switch (x) {
    case -4: return 1; case -3: return 2; case -2: return 3; case -1: return 4;
    case 0: return 5; case 1: return 6; case 2: return 7; case 3: return 8;
    default: return 0;
    }
}
int swuc(unsigned char x)
{
    switch (x) {
    case 250: return 1; case 251: return 2; case 252: return 3;
    case 253: return 4; case 254: return 5; case 255: return 6;
    case 249: return 7; case 248: return 8;
    default: return 0;
    }
}
int sws(short x)
{
    switch (x) {
    case -32768: return 1; case -32767: return 2; case -32766: return 3;
    case -32765: return 4; case -32764: return 5; case -32763: return 6;
    case -32762: return 7; case -32761: return 8;
    default: return 0;
    }
}

/* ---- a default that cannot happen: no range test ---- */
int swur(int x, int a)
{
    switch (x) {
    case 2: return a + 20;
    case 3: return a + 30;
    case 4: return a * 4;
    case 5: return a - 50;
    case 6: return a ^ 60;
    case 7: return 70 - a;
    case 8: return a << 3;
    case 9: return a + 90;
    default: __builtin_unreachable();
    }
}
int ifur(int x, int a)
{
    if (x == 2) return a + 20;
    if (x == 3) return a + 30;
    if (x == 4) return a * 4;
    if (x == 5) return a - 50;
    if (x == 6) return a ^ 60;
    if (x == 7) return 70 - a;
    if (x == 8) return a << 3;
    return a + 90;
}

/* ---- a value live across the dispatch into one case only ---- */
int live_across(int x, int a, int b)
{
    int t = a * 7 + b;          /* read only in case 5 */
    int u = a - b;              /* read only in the default */
    switch (x) {
    case 0: return 1;
    case 1: return a;
    case 2: return b;
    case 3: return a + b;
    case 4: return a * b;
    case 5: return t;
    case 6: return 6;
    case 7: return 7;
    default: return u;
    }
}
int live_ref(int x, int a, int b)
{
    if (x == 0) return 1;
    if (x == 1) return a;
    if (x == 2) return b;
    if (x == 3) return a + b;
    if (x == 4) return a * b;
    if (x == 5) return a * 7 + b;
    if (x == 6) return 6;
    if (x == 7) return 7;
    return a - b;
}

/* ---- an interpreter: fallthrough, continue, return, a nested switch ---- */
enum { O_ADD, O_SUB, O_MUL, O_XOR, O_SHL, O_NEG, O_SKIP, O_DUP2, O_SEL,
       O_INC, O_END };
int interp(const unsigned char *pc, int acc)
{
    int k = 3, steps = 0;
    for (;; pc += 2) {
        int arg = pc[1];
        steps++;
        switch (pc[0]) {
        case O_ADD: acc += arg; break;
        case O_SUB: acc -= arg; break;
        case O_MUL: acc *= arg; break;
        case O_XOR: acc ^= arg << 4; break;
        case O_SHL: acc <<= (arg & 7); break;
        case O_NEG: acc = -acc; break;
        case O_SKIP: pc += 2; continue;
        case O_DUP2: acc += acc;        /* falls through */
        case O_INC: acc++; k += arg; break;
        case O_SEL:
            switch (arg) {
            case 10: acc += 1; break;
            case 11: acc += 20; break;
            case 12: acc += 300; break;
            case 13: acc += 4000; break;
            case 14: acc += 50000; break;
            case 15: acc ^= 0x55; break;
            case 16: acc ^= 0xaa00; break;
            case 17: acc -= k; break;
            default: acc = ~acc; break;
            }
            break;
        case O_END: return acc + k * 1000 + steps;
        default: return -12345;
        }
    }
}
int interp_ref(const unsigned char *pc, int acc)
{
    int k = 3, steps = 0;
    for (;; pc += 2) {
        int arg = pc[1], op = pc[0];
        steps++;
        if (op == O_ADD) acc += arg;
        else if (op == O_SUB) acc -= arg;
        else if (op == O_MUL) acc *= arg;
        else if (op == O_XOR) acc ^= arg << 4;
        else if (op == O_SHL) acc <<= (arg & 7);
        else if (op == O_NEG) acc = -acc;
        else if (op == O_SKIP) pc += 2;
        else if (op == O_DUP2) { acc += acc; acc++; k += arg; }
        else if (op == O_INC) { acc++; k += arg; }
        else if (op == O_SEL) {
            if (arg == 10) acc += 1; else if (arg == 11) acc += 20;
            else if (arg == 12) acc += 300; else if (arg == 13) acc += 4000;
            else if (arg == 14) acc += 50000; else if (arg == 15) acc ^= 0x55;
            else if (arg == 16) acc ^= 0xaa00; else if (arg == 17) acc -= k;
            else acc = ~acc;
        } else if (op == O_END) return acc + k * 1000 + steps;
        else return -12345;
    }
}

/* ---- two tables in one function, floats live round them ---- */
float two(int x, int y, float f)
{
    float g = f * 0.5f;
    switch (x) {
    case 0: f += 1.0f; break;
    case 1: f += g; break;
    case 2: f -= g; break;
    case 3: f *= 3.0f; break;
    case 4: f = g; break;
    case 5: f = -f; break;
    case 6: f += 6.0f; break;
    case 7: f += 7.0f; break;
    }
    switch (y) {
    case 20: return f + g;
    case 21: return f - g;
    case 22: return f * g;
    case 23: return g;
    case 24: return f;
    case 25: return f + 25.0f;
    case 26: return f + 26.0f;
    case 27: return f + 27.0f;
    }
    return f + g + 1.0f;
}
float two_ref(int x, int y, float f)
{
    float g = f * 0.5f;
    if (x == 0) f += 1.0f; else if (x == 1) f += g; else if (x == 2) f -= g;
    else if (x == 3) f *= 3.0f; else if (x == 4) f = g; else if (x == 5) f = -f;
    else if (x == 6) f += 6.0f; else if (x == 7) f += 7.0f;
    if (y == 20) return f + g;
    if (y == 21) return f - g;
    if (y == 22) return f * g;
    if (y == 23) return g;
    if (y == 24) return f;
    if (y == 25) return f + 25.0f;
    if (y == 26) return f + 26.0f;
    if (y == 27) return f + 27.0f;
    return f + g + 1.0f;
}

/* ---- a function too long for four-byte table entries ---- */
static volatile int vsink[4];
#define S1(n) vsink[(n) & 3] = vsink[((n) + 1) & 3] + a * (n) + (b ^ (n));
#define S8(n) S1(n) S1(n + 1) S1(n + 2) S1(n + 3) S1(n + 4) S1(n + 5) S1(n + 6) S1(n + 7)
#define S64(n) S8(n) S8(n + 8) S8(n + 16) S8(n + 24) S8(n + 32) S8(n + 40) S8(n + 48) S8(n + 56)
#define S512(n) S64(n) S64(n + 64) S64(n + 128) S64(n + 192) S64(n + 256) S64(n + 320) S64(n + 384) S64(n + 448)
int big(int x, int a, int b)
{
    int r = 0;
    switch (x) {
    case 0: r = a; break;
    case 1: S512(0) r = 1; break;
    case 2: r = b; break;
    case 3: S512(512) r = 3; break;
    case 4: r = a + b; break;
    case 5: S512(1024) r = 5; break;
    case 6: r = a - b; break;
    case 7: S512(1536) r = 7; break;
    case 8: r = a ^ b; break;
    default: r = -1; break;
    }
    return r;
}
int big_ref(int x, int a, int b)
{
    if (x == 0) return a;
    if (x == 2) return b;
    if (x == 4) return a + b;
    if (x == 6) return a - b;
    if (x == 8) return a ^ b;
    if (x == 1 || x == 3 || x == 5 || x == 7) return x;
    return -1;
}

static const unsigned char prog[] = {
    O_ADD, 5, O_MUL, 7, O_SEL, 12, O_XOR, 9, O_SKIP, 0, O_ADD, 200,
    O_DUP2, 2, O_SEL, 17, O_SHL, 3, O_NEG, 0, O_SEL, 99, O_INC, 4,
    O_SUB, 77, O_SEL, 10, O_SEL, 16, O_SEL, 15, O_SEL, 11, O_SEL, 13,
    O_SEL, 14, O_SEL, 9, O_END, 0
};
static const unsigned char prog_bad[] = { O_ADD, 1, 200, 0 };

static unsigned fbits(float f) { union { float f; unsigned u; } v; v.f = f; return v.u; }

void test_main(void)
{
    int x, y, i;
    static const int edge[] = { -0x7fffffff - 1, -0x7fffffff, -0x7ffffff8,
        -65536, -32769, -32768, -257, -256, -129, -128, -1, 0, 1, 127, 128,
        255, 256, 32767, 32768, 65535, 0x7ffffff7, 0x7ffffff8, 0x7fffffff };

    for (x = -40; x <= 40; x++) {
        check(sw0(x), if0(x));
        check(sw3(x), if3(x));
        check(swneg(x), ifneg(x));
        check(swur(2 + (x & 7), x), ifur(2 + (x & 7), x));
        check(sw1000(x + 1000), if1000(x + 1000));
        check(sw1000(x), if1000(x));
        check(swmax(0x7fffffff - 40 + x), x >= 33 ? x - 32 : 0);
        check(swmin(-0x7fffffff - 1 + 40 + x), x >= -40 && x <= -33 ? x + 41 : 0);
        check(swu(0x80000000u + (unsigned)x), x >= 0 && x <= 7 ? x + 1 : 0);
        check(swc((signed char)x), x >= -4 && x <= 3 ? x + 5 : 0);
        check(swc((signed char)(x + 256)), x >= -4 && x <= 3 ? x + 5 : 0);
        check(swuc((unsigned char)(x + 250)),
              x >= 0 && x <= 5 ? x + 1 : x == -1 ? 7 : x == -2 ? 8 : 0);
        check(sws((short)(x - 32768)), x >= 0 && x <= 7 ? x + 1 : 0);
        check(live_across(x, 100 + x, 7 - x), live_ref(x, 100 + x, 7 - x));
        check(big(x, 9 + x, 3 * x), big_ref(x, 9 + x, 3 * x));
        for (y = 15; y <= 32; y++)
            check(fbits(two(x, y, 1.5f + x)), fbits(two_ref(x, y, 1.5f + x)));
    }
    for (i = 0; i < (int)(sizeof edge / sizeof edge[0]); i++) {
        x = edge[i];
        check(sw0(x), if0(x));
        check(sw3(x), if3(x));
        check(swneg(x), ifneg(x));
        check(sw1000(x), if1000(x));
        check(swmax(x), x >= 0x7ffffff8 ? x - 0x7ffffff7 : 0);
        check(swmin(x), x <= -0x7ffffff9 ? (int)((unsigned)x - 0x80000000u) + 1 : 0);
        check(swu((unsigned)x), (unsigned)x - 0x80000000u < 8 ? (int)((unsigned)x - 0x80000000u) + 1 : 0);
        check(live_across(x, 5, 9), live_ref(x, 5, 9));
        check(big(x, 5, 9), big_ref(x, 5, 9));
    }
    for (i = -3; i <= 3; i++) {
        check(interp(prog, i * 1000), interp_ref(prog, i * 1000));
        check(interp(prog_bad, i), -12345);
    }
    us(bad ? "SWTABBAD\n" : "SWTABOK\n");
}
