/* ffn2d.c - DeepSeek-V4.1-Flash's shared expert (Q5_K, real blocks) for one
 * token on a 3 x 3 grid of T9000s through three IMS C104s (ffn2d.sh): the
 * 2D sharding plan.py's full_2d() models, measured.
 *
 * Node (r, c) = (NODE / 3, NODE % 3).  F = 2304 is 9 blocks of 256, D = 5120
 * is 20.
 *   gate/up  node (r, c) holds F rows 768r.. (3 blocks) over D's K-slice c
 *            (blocks 7c.., 7/7/6).  Its partial sums are reduce-scattered
 *            in row r, so it ends with F block 3r + c summed; SwiGLU makes
 *            that block of h.
 *   h        down's K-slice c' is F blocks c', c' + 3, c' + 6 - the blocks
 *            column c' holds - so node (r, c) quantises its h block to Q8_K
 *            and all-gathers it in column c: plan.py's input all-gather.
 *   down     node (r, c) holds D parts 3r..3r+2 (of 9) over its K-slice and
 *            reduce-scatters them in row r, ending with part 3r + c of y.
 * Each collective is one __vxfer over a virtual link per peer and plane,
 * every message split across the three planes, lists built once.  Partial
 * sums add in column order, so node 0 can check y bit for bit against the
 * same sums computed alone (-DVERIFY).  Every phase, collectives apart from
 * the compute around them, prints a T4_T9K_MARKS line. */
#include <conc.h>
#include <t9000.h>
#include "gguf/gguf_all_formats.inc"
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void us(const char *s) { while (*s) UART_DATA = (unsigned)(unsigned char)*s++; }
static unsigned f2b(float f) { union { float f; unsigned u; } v; v.f = f; return v.u; }
void __vsetlen(int n);
void __vsetfmt(unsigned fmt);
void __qsetfmt(int ggml_type);
int __qgemm(float *dst, const void *param, unsigned nblocks);
void __vq8k(void *dst, const void *src);
void __vsilu(float *dst, const float *src);
void __vmul(float *dst, const float *a, const float *b);
void __vadd(float *dst, const float *a, const float *b);
void *memcpy(void *d, const void *s, unsigned n);

#define FMT 13                          /* q5_K */
#define BSZ 176
#define Q8  292                         /* block_q8_K */
#define D   5120
#define F   2304
#define NBD (D / 256)                   /* 20 */
#define NBF (F / 256)                   /* 9 */
#define G   3                           /* grid side */
#define P   (G * G)
#define R   (NODE / G)
#define C   (NODE % G)

/* D's K-slices for gate/up, in blocks: 7, 7, 6 */
static const int k0[G + 1] = { 0, 7, 14, 20 };
/* D's 9 row parts for down: part i is rows pb[i].. */
#define PB(i)   ((i) * D / P)

static unsigned char Wg[3 * 256 * 7 * BSZ], Wu[3 * 256 * 7 * BSZ];
static unsigned char Wd[(PB(3) + 8) * 3 * BSZ];
static float x[D];
static unsigned char xq[NBD * Q8];
static float pu[G][2][256];             /* gate/up partials, by owner column */
static float rpu[G][2][256];            /* theirs for our block, by sender */
static float g[256], u[256], h[256];
static unsigned char hin[G * Q8];       /* our K-slice of h, by row; ours
                                         * is block R */
static float yp[G][PB(1) + 2];          /* down partials, by owner column */
static float ryp[G][PB(1) + 2];
static float y[PB(1) + 2];              /* our part of y */
static struct { const void *acts; const void *weights; } prm;
static int kfmt;
#if VERIFY && NODE == 0                 /* every node sends node 0 its y */
static float yall[D];
static unsigned char Wr[F * 7 * BSZ];   /* one slice of one matrix */
static float rp[G][F], rg[F], ru[F], rh[F], ry[G][D], yref[D];
static unsigned char rhq[NBF * Q8];
static unsigned char rhk[G][G * Q8];    /* K-slice c's blocks of rhq */
#endif

static const unsigned char *blk(int mat, int row, int b)
{
    return qall_blk[kfmt] + ((row * 7 + b * 3 + mat) % QALL_NB) * BSZ;
}

/* nr rows of nb blocks: b0, b0 + bs, ... */
static void fill(unsigned char *w, int mat, int r0, int nr, int b0, int nb,
                 int bs)
{
    int r, b;
    for (r = 0; r < nr; r++)
        for (b = 0; b < nb; b++)
            memcpy(w + (r * nb + b) * BSZ, blk(mat, r0 + r, b0 + b * bs), BSZ);
}

static const void *last_acts;           /* the keep bit, as ffn_cube.c */

static void gemm(float *out, const void *w, const void *acts, int rows, int nb)
{
    prm.acts = acts == last_acts ? (const char *)acts + 1 : acts;
    last_acts = acts;
    prm.weights = w;
    __qsetfmt(FMT);
    __vsetfmt(3);
    __vsetlen(rows);
    __qgemm(out, &prm, nb);
    __vsetfmt(0);
}

static void quant(void *dst, const void *src, int n)
{
    __vsetfmt(0);
    __vsetlen(n);
    __vq8k(dst, src);
    last_acts = 0;
}

/* ---- the network: a virtual link per peer node and plane ------------- */
/* VL(p, q) joins this node to node p on plane q (1..3); node p's link back
 * is VL(NODE, q), which is the header's second byte. */
#define NVL     (P * 3)
#define VL(p, q)        ((p) * 3 + (q) - 1)
static unsigned vlpkt[NVL][8];

static void setup(void)
{
    unsigned char hdr[2];
    int p, q;

    __stconf(T9_CR_MEMSTART, (unsigned)T9_VIN(NVL));
    t9_vcp_channels(NVL);
    for (q = 1; q <= 3; q++) {
        t9_link_virtual(q, 0);
        __stconf(T9_CR_VCPLINKMAXHDR(q), NVL - 1);
    }
    for (p = 0; p < P; p++)
        for (q = 1; q <= 3; q++) {
            if (p == NODE)
                continue;
            hdr[0] = (unsigned char)p;              /* the switch routes on it */
            hdr[1] = (unsigned char)VL(NODE, q);    /* and deletes it */
            t9_vlink(VL(p, q), vlpkt[VL(p, q)], q, hdr, 2);
        }
    t9_vcp_start(NVL);
}

/* list entries for `bytes` from src to node p or from p into dst, a third
 * on each plane */
static int part3(t9_xfer *l, int n, int p, int out, void *buf, int bytes)
{
    int q, off = 0, third = (bytes / 3) & ~3;

    for (q = 1; q <= 3; q++) {
        int b = q < 3 ? third : bytes - 2 * third;

        l[n].chan = out ? T9_VOUT(VL(p, q)) : T9_VIN(VL(p, q));
        l[n].buf = (char *)buf + off;
        l[n++].len = (unsigned)b;
        off += b;
    }
    return n;
}

#define NODE_AT(r, c)   ((r) * G + (c))
static t9_xfer l_rs1[4 * 3], l_ag[4 * 3], l_rs2[4 * 3];
static int n_rs1, n_ag, n_rs2;

static void plan_lists(void)
{
    int c2, r2;

    /* gate/up reduce-scatter in row R: our partials for (R, c2)'s block go
     * there, theirs for ours come back */
    for (c2 = 0; c2 < G; c2++) {
        if (c2 == C)
            continue;
        n_rs1 = part3(l_rs1, n_rs1, NODE_AT(R, c2), 1, pu[c2], sizeof pu[c2]);
        n_rs1 = part3(l_rs1, n_rs1, NODE_AT(R, c2), 0, rpu[c2], sizeof rpu[c2]);
    }
    /* the all-gather in column C: our h block (F block 3R + C) is row R's
     * of the K-slice, and the column's others send theirs */
    for (r2 = 0; r2 < G; r2++) {
        if (r2 == R)
            continue;
        n_ag = part3(l_ag, n_ag, NODE_AT(r2, C), 1, hin + R * Q8, Q8);
        n_ag = part3(l_ag, n_ag, NODE_AT(r2, C), 0, hin + r2 * Q8, Q8);
    }
    /* down reduce-scatter in row R: part 3R + c2 */
    for (c2 = 0; c2 < G; c2++) {
        int rows = PB(3 * R + c2 + 1) - PB(3 * R + c2);
        int mine = PB(3 * R + C + 1) - PB(3 * R + C);

        if (c2 == C)
            continue;
        n_rs2 = part3(l_rs2, n_rs2, NODE_AT(R, c2), 1, yp[c2], rows * 4);
        n_rs2 = part3(l_rs2, n_rs2, NODE_AT(R, c2), 0, ryp[c2], mine * 4);
    }
}

/* our block of gate (k = 0) or up (1) summed in column order:
 * ((p0 + p1) + p2) */
static void sum3(float *dst, int k)
{
    int c2;

    __vsetlen(256);
    memcpy(dst, C == 0 ? pu[0][k] : rpu[0][k], 256 * 4);
    for (c2 = 1; c2 < G; c2++)
        __vadd(dst, dst, C == c2 ? pu[c2][k] : rpu[c2][k]);
}

int test_main(void)
{
    int i, c2, nb = k0[C + 1] - k0[C], bad = 0;
    int r0 = PB(3 * R), mine = PB(3 * R + C + 1) - PB(3 * R + C);

    for (kfmt = 0; kfmt < QALL_N && qall_fmt[kfmt] != FMT; kfmt++)
        ;
    for (i = 0; i < D; i++)
        x[i] = (float)((i % 31) - 15) * 0.0078125f;
    fill(Wg, 0, R * 768, 768, k0[C], nb, 1);
    fill(Wu, 1, R * 768, 768, k0[C], nb, 1);
    fill(Wd, 2, r0, PB(3 * R + 3) - r0, C, G, G);
    quant(xq, x, D);                        /* the token, already here */
    setup();
    plan_lists();
    {                                       /* barrier: one timeline */
        static unsigned t[P][3];
        t9_xfer l[(P - 1) * 2 * 3];
        int n = 0, p;
        for (p = 0; p < P; p++)
            if (p != NODE) {
                n = part3(l, n, p, 1, t[NODE], 12);
                n = part3(l, n, p, 0, t[p], 12);
            }
        __vxfer(l, (unsigned)n);
    }
    us("start\n");

    /* gate and up of F rows 768R.., one block of rows per owner */
    for (c2 = 0; c2 < G; c2++) {
        gemm(pu[c2][0], Wg + c2 * 256 * nb * BSZ, xq + k0[C] * Q8, 256, nb);
        gemm(pu[c2][1], Wu + c2 * 256 * nb * BSZ, xq + k0[C] * Q8, 256, nb);
    }
    us("gateup\n");
    __vxfer(l_rs1, (unsigned)n_rs1);
    us("rs1\n");
    sum3(g, 0);
    sum3(u, 1);
    __vsilu(g, g);
    __vmul(h, g, u);
    quant(hin + R * Q8, h, 256);
    us("swiglu\n");
    __vxfer(l_ag, (unsigned)n_ag);
    us("ag\n");
    for (c2 = 0; c2 < G; c2++)
        gemm(yp[c2], Wd + (PB(3 * R + c2) - r0) * 3 * BSZ, hin,
             PB(3 * R + c2 + 1) - PB(3 * R + c2), 3);
    us("down\n");
    __vxfer(l_rs2, (unsigned)n_rs2);
    us("rs2\n");
    __vsetlen(mine);
    memcpy(y, C == 0 ? yp[0] : ryp[0], mine * 4);
    for (c2 = 1; c2 < G; c2++)
        __vadd(y, y, C == c2 ? yp[c2] : ryp[c2]);
    us("done\n");

#if VERIFY
    /* every part to node 0, then node 0 alone, the same sums in order */
    {
        t9_xfer l[(P - 1) * 3];
        int n = 0;
#if NODE == 0
        int p;
        memcpy(yall, y, mine * 4);
        for (p = 1; p < P; p++)
            n = part3(l, n, p, 0, yall + PB(p), (PB(p + 1) - PB(p)) * 4);
#else
        n = part3(l, n, 0, 1, y, mine * 4);
#endif
        __vxfer(l, (unsigned)n);
    }
#if NODE == 0
    {
        for (c2 = 0; c2 < G; c2++) {
            int b = k0[c2 + 1] - k0[c2];
            fill(Wr, 0, 0, F, k0[c2], b, 1);
            gemm(rp[c2], Wr, xq + k0[c2] * Q8, F, b);
        }
        __vsetlen(F);
        memcpy(rg, rp[0], sizeof rg);
        __vadd(rg, rg, rp[1]);
        __vadd(rg, rg, rp[2]);
        for (c2 = 0; c2 < G; c2++) {
            int b = k0[c2 + 1] - k0[c2];
            fill(Wr, 1, 0, F, k0[c2], b, 1);
            gemm(rp[c2], Wr, xq + k0[c2] * Q8, F, b);
        }
        __vsetlen(F);
        memcpy(ru, rp[0], sizeof ru);
        __vadd(ru, ru, rp[1]);
        __vadd(ru, ru, rp[2]);
        __vsilu(rg, rg);
        __vmul(rh, rg, ru);
        quant(rhq, rh, F);
        for (c2 = 0; c2 < G; c2++) {
            static unsigned char Wdr[D * G * BSZ];
            for (i = 0; i < G; i++)
                memcpy(rhk[c2] + i * Q8, rhq + (c2 + G * i) * Q8, Q8);
            fill(Wdr, 2, 0, D, c2, G, G);
            gemm(ry[c2], Wdr, rhk[c2], D, G);
        }
        __vsetlen(D);
        memcpy(yref, ry[0], sizeof yref);
        __vadd(yref, yref, ry[1]);
        __vadd(yref, yref, ry[2]);
        for (i = 0; i < D; i++)
            if (f2b(yref[i]) != f2b(yall[i]))
                bad++;
        us(bad ? "VERIFY BAD\n" : "VERIFY OK\n");
    }
#endif
#endif
    return bad;
}
