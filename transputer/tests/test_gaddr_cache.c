/* Expected: SGADDROK\nH */
/*
 * tp_gaddr_cache keeps a busy global's address in a frame word of its own:
 * the words go on top of the frame, so the return address and every
 * parameter move up, the first use's relocation moves to a load after the
 * prologue and the others' are deleted.  Each function here uses some
 * global in a loop or three times, around what that renumbering must keep
 * right: parameters past the third (above the frame), a local array whose
 * address escapes and its one-past-the-end pointer (an LDLP of the old
 * frame top, which must not move), float locals the FPU reaches by
 * address, early returns, recursion, varargs, and more busy globals than
 * the pass keeps (four).
 */
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg   __builtin_va_arg
#define va_end   __builtin_va_end

static int table[16];
static int count;
static float scale[4] = { 0.5f, 1.5f, 2.5f, -3.5f };
static int g1, g2, g3, g4, g5;
static volatile int one = 1;

static int fill(int n)
{
    int i;
    for (i = 0; i < 16; i++)
        table[i] = i * 3 + count++;
    table[0] += table[15];
    return count + n;
}

static int six(int a, int b, int c, int d, int e, int f)
{
    int i, s = 0;
    if (a < 0)
        return table[0] + table[1] + table[2] - f;
    for (i = 0; i < 16; i++)
        s += table[i] * (i & 3);
    return s + a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f;
}

static int sumto(const int *p, const int *end)
{
    int s = 0;
    while (p < end)
        s = s * 3 + *p++;
    return s;
}

static int locals(int k)
{
    int buf[8], *end = &buf[8], i;
    for (i = 0; i < 8; i++)
        buf[i] = table[i + k] - table[i];
    if (end - buf != 8)
        return -1;
    return sumto(buf, end) + table[15];
}

static int fpart(float x)
{
    float acc = 0.0f, t[2];
    int i;
    for (i = 0; i < 8; i++) {
        t[i & 1] = scale[i & 3] * x;
        acc = acc + t[i & 1] * scale[(i + 1) & 3];
    }
    return (int)(acc * 4.0f) + (int)t[0];
}

static int many(int x)
{
    g1 = x; g2 = g1 + 1; g3 = g2 + g1; g4 = g3 + g2; g5 = g4 + g3;
    g1 += g5; g2 += g4; g3 += g1; g4 += g2; g5 += g3;
    return g1 + g2 + g3 + g4 + g5;
}

static int rec(int d)
{
    if (d == 0)
        return table[d] + table[1];
    return table[d & 15] + 2 * rec(d - 1) - table[(d + 1) & 15];
}

static int vsum(int n, ...)
{
    va_list ap;
    int s = 0, i;
    va_start(ap, n);
    for (i = 0; i < n; i++)
        s += va_arg(ap, int) * table[i];
    va_end(ap);
    return s + table[n];
}

static int results(int *r)
{
    r[0] = fill(one);
    r[1] = six(one, 2, 3, 4, 5, 6);
    r[2] = six(-one, 2, 3, 4, 5, 6);
    r[3] = locals(one * 5);
    r[4] = fpart(3.0f * one);
    r[5] = many(one * 7);
    r[6] = rec(9 * one);
    r[7] = vsum(5, 1, -2, 3, -4, 5 * one);
    return 8;
}

#ifdef HOST
#include <stdio.h>
int main(void)
{
    int r[8], i, n = results(r);
    for (i = 0; i < n; i++)
        printf("%d, ", r[i]);
    printf("\n");
    return 0;
}
#else
void test_main(void)
{
    /* from gcc on the host, -DHOST */
    static const int want[8] = {
        17, 891, 66, -65560, -137, 288, 30724, 128
    };
    volatile unsigned *uart = (volatile unsigned *)0x10000000u;
    const char *msg = "GADDROK\n";
    int r[8], i, n = results(r);

    for (i = 0; i < n; i++)
        if (r[i] != want[i])
            msg = "GADDRBAD\n";
    while (*msg)
        *uart = *msg++;
}
#endif
