/* Expected: SPACKEDOK\nH */
/*
 * Word, float, pointer, long long and double members of packed and
 * #pragma pack(2) structs.  Such a member can sit at any byte (packed) or
 * any even byte (pack(2)), but ldnl/stnl ignore the low two address bits,
 * so they read and write the aligned word around it: `p->k` with k at
 * offset 1 loaded bytes 0..3.  Loads, stores, compound assignment,
 * increments, members of nested and arrayed packed structs, and members
 * reached through a pointer, all at odd and two-aligned offsets.  Every
 * byte of every struct goes into a hash checked against gcc's on the host
 * (-DHOST), so a store that spills onto a neighbour shows too.
 */
typedef unsigned int u32;
#ifdef HOST
typedef u32 IPTR;               /* a 4-byte stand-in: the hash covers bytes */
#define DEREF(p) target
#define PTRVAL(v) (v)
#else
typedef int *IPTR;
#define DEREF(p) (*(p))
#define PTRVAL(v) ((int *)(v))
#endif

struct __attribute__((packed)) P {
    char c;
    int k;
    unsigned u;
    char d;
    float f;
    short s;
    IPTR ptr;
    long long ll;
    char e;
    double db;
};

#pragma pack(2)
struct Q {
    short s;
    int k;
    unsigned u;
    short t;
    float f;
    long long ll;
    double db;
};
#pragma pack()

struct __attribute__((packed)) N {
    char a;
    struct Q q;
    char b;
    int k;
};

static struct P gp[3];
static struct Q gq[3];
static int target = 77;
static struct N gn;

static u32 mix(u32 h, u32 v)
{
    return (h ^ v) * 16777619u;
}

static u32 bytes(u32 h, const void *p, int n)
{
    const unsigned char *b = p;
    while (n--)
        h = mix(h, *b++);
    return h;
}

static void fill_p(struct P *p, int i)
{
    p->c = (char)(i + 1);
    p->k = -123456 * (i + 1);
    p->u = 0xC0FFEE00u + (u32)i;
    p->d = (char)(i + 2);
    p->f = 1.5f * (float)(i + 1);
    p->s = (short)(300 + i);
#ifdef HOST
    p->ptr = 1;
#else
    p->ptr = &target;
#endif
    p->ll = 0x0123456789ABCDEFLL * (i + 1);
    p->e = (char)(i + 3);
    p->db = 2.25 * (i + 1);
}

static void fill_q(struct Q *q, int i)
{
    q->s = (short)(7 + i);
    q->k = 0x1234567 + i;
    q->u = 0x89ABCDEFu - (u32)i;
    q->t = (short)(-9 - i);
    q->f = -0.75f * (float)(i + 2);
    q->ll = -0x1122334455667788LL + i;
    q->db = 1.0 / (i + 3);
}

static u32 run(void)
{
    u32 h = 2166136261u;
    struct P lp;
    struct Q lq;
    int i;

    for (i = 0; i < 3; i++) {
        fill_p(&gp[i], i);
        fill_q(&gq[i], i);
    }
    fill_p(&lp, 5);
    fill_q(&lq, 5);
    gn.a = 'a';
    gn.b = 'b';
    {
        struct Q t;             /* &gn.q itself would be misaligned */
        fill_q(&t, 9);
        gn.q = t;
    }
    gn.k = 424242;

    /* compound assignment and increments */
    gp[1].k += 1000;
    gp[2].u ^= 0xFFFF0000u;
    gp[0].k++;
    --gq[1].k;
    lp.u <<= 3;
    lq.k *= 3;
    gn.q.k -= 5;
    gn.k += gn.q.k;
    lp.f += 0.5f;
    lq.ll += 1;
    gp[1].ll -= gp[0].ll;
    gq[2].db *= 2.0;

    /* loads into arithmetic and through a pointer */
    h = mix(h, (u32)(gp[0].k + gp[1].k + gp[2].k));
    h = mix(h, gp[1].u + lq.u);
    h = mix(h, (u32)(int)(gp[2].f * 100.0f + lq.f * 10.0f));
    h = mix(h, (u32)DEREF(gp[1].ptr));
    h = mix(h, (u32)(gq[0].ll >> 32) ^ (u32)gq[0].ll);
    h = mix(h, (u32)(int)(gq[1].db * 1000.0));
    h = mix(h, (u32)(lp.k - lq.k));
    h = mix(h, (u32)(gn.q.k + gn.k));
    {
        struct P *pp = &gp[2];
        struct Q *qq = &gq[1];
        pp->k = qq->k + pp->s;
        qq->u = pp->u;
        h = mix(h, (u32)(pp->k ^ (int)qq->u));
    }

    /* every byte, neighbours included (the pointers made equal first) */
    for (i = 0; i < 3; i++)
        gp[i].ptr = PTRVAL(0x12345678u);
    lp.ptr = PTRVAL(0x12345678u);
    h = bytes(h, gp, sizeof gp);
    h = bytes(h, gq, sizeof gq);
    h = bytes(h, &gn, sizeof gn);
    h = bytes(h, &lp, sizeof lp);
    h = bytes(h, &lq, sizeof lq);
    return h;
}

#ifdef HOST
#include <stdio.h>
int main(void)
{
    printf("0x%08X sizes %d %d %d\n", run(), (int)sizeof(struct P),
           (int)sizeof(struct Q), (int)sizeof(struct N));
    return 0;
}
#else
void test_main(void)
{
    volatile unsigned *uart = (volatile unsigned *)0x10000000u;
    /* gcc on the host, -DHOST, at -O0 with UBSan and at -O2 */
    const char *msg = run() == 0x83B1E0D5u && sizeof(struct P) == 37 &&
                      sizeof(struct Q) == 32 && sizeof(struct N) == 38
                      ? "PACKEDOK\n" : "PACKEDBAD\n";
    while (*msg)
        *uart = *msg++;
}
#endif
