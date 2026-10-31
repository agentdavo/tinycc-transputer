/* allred.c - all-reduce of a small vector (24 floats, a layer's hc mixes)
 * on 8 T9000s through three IMS C104s (allred.sh), the two ways plan.py's
 * allreduce_c104() weighs for small vectors:
 *   rd   recursive doubling with the whole vector: 3 steps, each one
 *        __vxfer swapping it with NODE ^ 2^s, a third on each plane
 *   r8   radix 8: one __vxfer swapping it with all seven peers at once
 * REPS of each, a T4_T9K_MARKS line after each batch.  The transfer lists
 * are built once, as a layer loop would; a rep is a memcpy to reset the
 * vector, the exchanges and the adds.  The vectors hold small integers, so
 * every order of summing is exact, and every node checks the last total of
 * each batch.  Expected (node 0): ALLRED OK */
#include <conc.h>
#include <t9000.h>
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void us(const char *s) { while (*s) UART_DATA = (unsigned)(unsigned char)*s++; }
void __vsetlen(int n);
void __vadd(float *dst, const float *a, const float *b);
void *memcpy(void *d, const void *s, unsigned n);

#define P       8
#define V       24
#define REPS    32
/* one virtual link per peer NODE ^ d and plane q, as ffn_cube.c -DDIRECT */
#define NVL     21
#define VLD(d, q)       (((d) - 1) * 3 + (q) - 1)

static unsigned vlpkt[NVL][8];
static float mine[V], acc[V], in[P][V];
static t9_xfer rdl[3][6], r8l[2 * NVL];
static int bad;

static void setup(void)
{
    unsigned char hdr[2];
    int d, q;

    __stconf(T9_CR_MEMSTART, (unsigned)T9_VIN(NVL));
    t9_vcp_channels(NVL);
    for (q = 1; q <= 3; q++) {
        t9_link_virtual(q, 0);
        __stconf(T9_CR_VCPLINKMAXHDR(q), NVL - 1);
    }
    for (d = 1; d < P; d++)
        for (q = 1; q <= 3; q++) {
            hdr[0] = (unsigned char)(NODE ^ d); /* the switch routes on it */
            hdr[1] = (unsigned char)VLD(d, q);  /* and deletes it */
            t9_vlink(VLD(d, q), vlpkt[VLD(d, q)], q, hdr, 2);
        }
    t9_vcp_start(NVL);
}

/* list entries: src to peer NODE ^ d and its vector into dst, a third on
 * each plane */
static int pair(t9_xfer *l, int n, int d, const float *src, float *dst)
{
    int q;

    for (q = 0; q < 3; q++) {
        l[n].chan = T9_VOUT(VLD(d, q + 1));
        l[n].buf = (void *)(src + q * (V / 3));
        l[n++].len = V / 3 * 4;
        l[n].chan = T9_VIN(VLD(d, q + 1));
        l[n].buf = dst + q * (V / 3);
        l[n++].len = V / 3 * 4;
    }
    return n;
}

static void check(void)
{
    int i;
    for (i = 0; i < V; i++)
        if (acc[i] != (float)(36 * (i + 1)))   /* 1 + 2 + ... + 8 */
            bad++;
}

int test_main(void)
{
    int i, s, d, rep, n8 = 0;

    for (i = 0; i < V; i++)
        mine[i] = (float)((NODE + 1) * (i + 1));
    setup();
    for (s = 0; s < 3; s++)
        pair(rdl[s], 0, 1 << s, acc, in[0]);
    for (d = 1; d < P; d++)
        n8 = pair(r8l, n8, d, mine, in[d]);
    __vsetlen(V);
    memcpy(acc, mine, sizeof acc);          /* barrier: one timeline */
    __vxfer(rdl[0], 6);
    us("start\n");

    for (rep = 0; rep < REPS; rep++) {
        memcpy(acc, mine, sizeof acc);
        for (s = 0; s < 3; s++) {
            __vxfer(rdl[s], 6);
            __vadd(acc, acc, in[0]);
        }
    }
    us("rd\n");
    check();

    for (rep = 0; rep < REPS; rep++) {
        memcpy(acc, mine, sizeof acc);
        __vxfer(r8l, (unsigned)n8);
        for (d = 1; d < P; d++)
            __vadd(acc, acc, in[d]);
    }
    us("r8\n");
    check();

    us(bad ? "ALLRED BAD\n" : "ALLRED OK\n");
    return bad;
}
