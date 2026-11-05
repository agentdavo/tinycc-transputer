/* Expected: SSCHEDCOPYOK\nH */
/*
 * The scheduler's S11 recomputes an address where it is used instead of
 * parking it in a slot, and one-word struct copies are a load and a store.
 * findRange is the ray tracer's: `if (c > max) max = c;` jumps over the
 * copy into max, and a first cut of S11 replaced the later `ldl max` with
 * the copy's source, wrong on the path that jumped (the branch landed on
 * PFIX 0 padding the rule's scan stepped over).  The rest are the shapes
 * the two rules rewrite: conditional copies, `p[k] = <e>` stores and loads
 * through a parked address, one-word unions copied through pointers, local
 * and global arrays, and members of packed and #pragma pack(2) structs,
 * which must keep their byte-wise copy.  A hash over generated inputs is
 * checked against gcc's on the host (-DHOST).
 */
typedef unsigned int u32;
typedef union { int i; float f; } W;

static int findRange(int a, int b, int c, int d)
{
    int max, min;

    if (a > b) {
        max = a;
        min = b;
    } else {
        max = b;
        min = a;
    }
    if (c > max)
        max = c;
    else if (c < min)
        min = c;
    if (d > max)
        max = d;
    else if (d < min)
        min = d;
    return max - min;
}

static int pick(int c, int x, int y)
{
    int v = y;
    if (c & 1)
        v = x;
    if (c & 2)
        v = v + x;
    return v * 3 + 1;
}

static int store_after(int *p, int k, int v)
{
    p[k + 2] = v * 5 - p[k];
    p[k + 1] = p[k + 2] ^ v;
    return p[k + 1] + p[k + 2];
}

static void wcopy(W *d, const W *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        d[i + 1] = s[i];
        d[i + 1].i ^= (int)0x80000000;
    }
}

static W gsrc[8], gdst[9];

struct __attribute__((packed)) P { char c; W w; char e; };
#pragma pack(2)
struct Q { short s; W w; short t; };
#pragma pack()

static u32 rs = 12345;
static u32 rnd(void)
{
    rs = rs * 1103515245u + 12345u;
    return rs >> 8;
}

static u32 mix(u32 h, u32 v)
{
    return (h ^ v) * 16777619u;
}

static u32 run(void)
{
    u32 h = 2166136261u;
    int i, k, buf[12];
    W local[4], out[5];
    struct P p[2];
    struct Q q[2];

    for (i = 0; i < 400; i++) {
        int a = (int)(rnd() % 2001) - 1000, b = (int)(rnd() % 2001) - 1000;
        int c = (int)(rnd() % 2001) - 1000, d = (int)(rnd() % 2001) - 1000;
        h = mix(h, (u32)findRange(a, b, c, d));
        h = mix(h, (u32)pick((int)rnd(), a, b));
    }
    for (k = 0; k < 9; k++) {
        for (i = 0; i < 12; i++)
            buf[i] = (int)rnd() - 0x400000;
        h = mix(h, (u32)store_after(buf, k, (int)rnd()));
        for (i = 0; i < 12; i++)
            h = mix(h, (u32)buf[i]);
    }
    for (i = 0; i < 8; i++)
        gsrc[i].i = (int)rnd() * 3;
    wcopy(gdst, gsrc, 8);
    for (i = 0; i < 4; i++)
        local[i].f = (float)(int)(rnd() % 1000) * 0.25f;
    wcopy(out, local, 4);
    for (i = 0; i < 9; i++)
        h = mix(h, (u32)gdst[i].i);
    for (i = 1; i < 5; i++)
        h = mix(h, (u32)out[i].i);
    p[0].c = 1; p[0].e = 2; p[1].c = 3; p[1].e = 4;
    q[0].s = 5; q[0].t = 6; q[1].s = 7; q[1].t = 8;
    p[0].w = gsrc[2];
    p[1].w = p[0].w;
    q[0].w = local[1];
    q[1].w = q[0].w;
    /* Read back through aligned copies: a 32-bit load of a packed member
     * is a separate, older problem (it uses ldnl on the odd address). */
    gdst[0] = p[1].w;
    out[0] = q[1].w;
    h = mix(h, (u32)(p[0].c + p[0].e + p[1].c + p[1].e));
    h = mix(h, (u32)(q[0].s + q[0].t + q[1].s + q[1].t));
    h = mix(h, (u32)gdst[0].i);
    h = mix(h, (u32)out[0].i);
    return h;
}

#ifdef HOST
#include <stdio.h>
int main(void)
{
    printf("0x%08X\n", run());
    return 0;
}
#else
void test_main(void)
{
    volatile unsigned *uart = (volatile unsigned *)0x10000000u;
    /* gcc on the host, -DHOST, at -O0 with UBSan and at -O2 */
    const char *msg = run() == 0x47BD81F3u ? "SCHEDCOPYOK\n" : "SCHEDCOPYBAD\n";
    while (*msg)
        *uart = *msg++;
}
#endif
