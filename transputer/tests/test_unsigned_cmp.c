/*
 * test_unsigned_cmp.c - unsigned <, <=, >, >= on 32- and 64-bit values
 * around the sign boundary, as values, as branches and against literals,
 * hashed and checked against a hash from an x86-64 host.
 *
 * Expected: SUCMPOK\nH
 *
 * -mt9000 compares with gtu; the T800/T805/T425 have no such instruction,
 * so -mt800 and -mt425 flip both sign bits (mint; xor) and use gt.
 */
typedef unsigned int u32;
typedef unsigned long long u64;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uc("0123456789abcdef"[(v >> i) & 15]);
}

static const u32 v[12] = {
    0, 1, 2, 1000, 0x7ffffffe, 0x7fffffff, 0x80000000, 0x80000001,
    0x9abcdef0, 0xfffffffe, 0xffffffff, 0x12345678
};

static u32 h = 0x811c9dc5;
static void mix(u32 x) { h = (h ^ x) * 0x01000193; }

static u32 rel(u32 a, u32 b)
{
    return (a < b) | (a <= b) << 1 | (a > b) << 2 | (a >= b) << 3;
}

static u32 branches(u32 a, u32 b)
{
    u32 r = 0;
    if (a < b) r |= 1;
    if (a <= b) r |= 2;
    if (a > b) r |= 4;
    if (a >= b) r |= 8;
    if (!(a < b)) r |= 16;
    while (a > b) {             /* loop condition */
        r |= 32;
        break;
    }
    return r;
}

#define LIT(k) ((a < (k)) | (a <= (k)) << 1 | (a > (k)) << 2 | (a >= (k)) << 3 \
                | ((k) < a) << 4 | ((k) >= a) << 5)
static void lits(u32 a)
{
    mix(LIT(0u) | LIT(1u) << 6 | LIT(0x7fffffffu) << 12);
    mix(LIT(0x80000000u) | LIT(0xffffffffu) << 6 | LIT(1000u) << 12);
}

static u32 rel64(u64 a, u64 b)
{
    return (a < b) | (a <= b) << 1 | (a > b) << 2 | (a >= b) << 3;
}

void test_main(void)
{
    volatile u32 a, b;
    int i, j;

    for (i = 0; i < 12; i++) {
        a = v[i];
        lits(a);
        for (j = 0; j < 12; j++) {
            b = v[j];
            mix(rel(a, b));
            mix(branches(a, b));
            mix(rel64((u64)a << 32 | b, (u64)b << 32 | a));
            mix(rel64((u64)a << 32 | b, (u64)a << 32 | a));
        }
    }
    if (h == 0xefb0dc80)
        us("UCMPOK\n");
    else {
        us("FAIL ");
        uh(h);
        uc('\n');
    }
}
