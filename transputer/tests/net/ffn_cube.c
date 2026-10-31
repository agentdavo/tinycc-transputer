/* ffn_cube.c - DeepSeek-V4.1-Flash's shared expert (Q5_K, real blocks) for
 * one token, sharded over an 8-node hypercube: node i's link d+1 goes to
 * node i ^ (1 << d), link 0 is its host (cube.map).
 *
 *   SCHEME=1 (1D)  node i computes rows i*288.. of gate and up over the
 *                  whole input, rows i*640.. of down; h and y are
 *                  all-gathered over all three dimensions.
 *   SCHEME=2 (2D)  gate and up on a 4 x 2 grid: node (r, c) = (i>>1, i&1)
 *                  takes rows r*576.. over input half c, the pair reduce-
 *                  scatters its partial sums over dimension 0 so node i
 *                  again owns h rows i*288..; down rows go so that column c
 *                  owns y half c, which it gathers over dimensions 1 and 2 -
 *                  all a 2D layer needs as the next input.
 *
 * Collectives are recursive doubling, so every exchange is with a direct
 * neighbour.  Every phase prints a line for T4_T9K_MARKS.  With -DVERIFY,
 * node 0 then recomputes its output alone - summing the 2D halves in the
 * same order - and checks it bit for bit.
 *
 * -DC104 (scheme 1): no hypercube.  Links 1-3 each go to an IMS C104, one
 * switch per link (c104.sh), and nodes talk over virtual links through the
 * switches.  Each node has a virtual link per peer, plane and part, so the
 * all-gather is two crossbar steps: within groups of four, one peer per
 * plane, then a group's four pieces to the partner group split over the
 * three planes.  -DSPLIT=k sends each message as k parts on k virtual
 * links.  Each step's transfers go as one __vxfer (VXFER=0: a process
 * each). */
#include <conc.h>
#include "gguf/gguf_all_formats.inc"
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void us(const char *s) { while (*s) UART_DATA = (unsigned)(unsigned char)*s++; }
static unsigned f2b(float f) { union { float f; unsigned u; } v; v.f = f; return v.u; }
void __in(int len, Channel *c, void *buf);
void __out(int len, Channel *c, const void *buf);
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
#define D   5120
#define F   2304
#define NBD (D / 256)                   /* 20 */
#define NBF (F / 256)                   /* 9 */
#define P   8
#ifndef SCHEME
#define SCHEME 1
#endif

static unsigned char Wg[F * NBD * BSZ / P * 2], Wu[F * NBD * BSZ / P * 2];
static unsigned char Wd[D * NBF * BSZ / P];
static float x[D], gp[F], up_[F], g[F], u[F], h[F], y[D], tmp[F];
static unsigned char xq[NBD * 292], hq[NBF * 292];
static struct { const void *acts; const void *weights; } prm;
static int kfmt;
#if VERIFY
static unsigned char RgL[F * NBD / 2 * BSZ], RgR[F * NBD / 2 * BSZ];
static unsigned char RuL[F * NBD / 2 * BSZ], RuR[F * NBD / 2 * BSZ];
static unsigned char Rd[D * NBF * BSZ];
static float ry[D], rg2[F], ru2[F];
#endif

/* the real block for (matrix, row, block) - the same on every node */
static const unsigned char *blk(int mat, int row, int b)
{
    return qall_blk[kfmt] + ((row * 7 + b * 3 + mat) % QALL_NB) * BSZ;
}

/* rows [r0, r0+nr) x blocks [b0, b0+nb) of matrix `mat`, row-major */
static void fill(unsigned char *w, int mat, int r0, int nr, int b0, int nb)
{
    int r, b;
    for (r = 0; r < nr; r++)
        for (b = 0; b < nb; b++)
            memcpy(w + (r * nb + b) * BSZ, blk(mat, r0 + r, b0 + b), BSZ);
}

/* KEEP=1: a QGEMM on the vector the last one read - up after gate, a
 * matrix's later row chunks - sets the keep bit (TVX_KEEP_ACTS in
 * include/tvx.h), and the activation buffer does not read it again.
 * quant() writes a vector, so it ends the promise. */
#ifndef KEEP
#define KEEP    1
#endif
static const void *last_acts;

static void quant(void *dst, const void *src)
{
    __vq8k(dst, src);
    last_acts = 0;
}

static void gemm(float *out, const void *w, const void *acts, int rows, int nb)
{
    prm.acts = KEEP && acts == last_acts ? (const char *)acts + 1 : acts;
    last_acts = acts;
    prm.weights = w;
    __qsetfmt(FMT);
    __vsetfmt(3);
    __vsetlen(rows);
    __qgemm(out, &prm, nb);
    __vsetfmt(0);
}

/* All-gather over the hypercube dimensions dims[0..nd): v holds pieces of
 * `len` floats in group order, this node's at index `me`. */
static void allgather(float *v, int len, int me, const int *dims, int nd)
{
    int s;
    for (s = 0; s < nd; s++) {
        int cnt = 1 << s, mine = (me >> s) << s, theirs = mine ^ cnt;
        __out(cnt * len * 4, LINKOUT(dims[s] + 1), v + mine * len);
        __in(cnt * len * 4, LINKIN(dims[s] + 1), v + theirs * len);
    }
}

#if C104
#if SCHEME != 1
#error "-DC104 implements scheme 1"
#endif
#include <t9000.h>
#ifndef VXFER
#define VXFER 1
#endif

#ifndef DIRECT
#define DIRECT  0
#endif
#ifndef OVERLAP
#define OVERLAP 0
#endif
#if (DIRECT || OVERLAP) && !VXFER
#error "-DDIRECT and -DOVERLAP use __vxfer"
#endif
#if OVERLAP && !DIRECT
#error "-DOVERLAP needs -DDIRECT: a chunk's exchange is one step"
#endif

/* Six virtual links fit below the default MemStart (#80000070); more move
 * MemStart up, which leaves the boot code below it alone - only channel
 * instructions look there.  One link's packets go stop-and-wait, so a
 * cable wants several in flight. */
#if DIRECT
/* One virtual link per peer and plane: VLD(d, q) joins this node to peer
 * NODE ^ d (d = 1..7) on plane q (1..3).  NODE ^ d reaches this node at
 * the same d, so both ends number it the same.  A piece goes to all seven
 * peers at once, a third on each plane: the two-step scheme's bytes in one
 * step. */
#define NVL     21
#define VLD(d, q)       (((d) - 1) * 3 + (q) - 1)
#else
/* SPLIT virtual links to each peer on each plane: VL(0, x, k) joins this
 * node to group peer NODE ^ x on plane x (x = 1..3), VL(1, p, k) to the
 * partner NODE ^ 4 on plane p.  Both ends number them the same way; a
 * message split over SPLIT of them keeps SPLIT packets in flight. */
#ifndef SPLIT
#define SPLIT   3
#endif
#define NVL     (6 * SPLIT)
#define VL(kind, plane, k)      (((kind) * 3 + (plane) - 1) * SPLIT + (k))
#endif
#define NJOBS   (2 * NVL)
static unsigned vlpkt[NVL][8];

static void c104_setup(void)
{
    unsigned char hdr[2];
    int x, k;

    if (NVL > 6)
        __stconf(T9_CR_MEMSTART, (unsigned)T9_VIN(NVL));
    t9_vcp_channels(NVL);
    for (x = 1; x <= 3; x++) {
        t9_link_virtual(x, 0);                  /* receive 1-byte headers */
        __stconf(T9_CR_VCPLINKMAXHDR(x), NVL - 1);
    }
#if DIRECT
    for (k = 1; k < P; k++)
        for (x = 1; x <= 3; x++) {
            hdr[0] = (unsigned char)(NODE ^ k); /* the switch routes on it */
            hdr[1] = (unsigned char)VLD(k, x);  /* and deletes it */
            t9_vlink(VLD(k, x), vlpkt[VLD(k, x)], x, hdr, 2);
        }
#else
    for (x = 1; x <= 3; x++)
        for (k = 0; k < SPLIT; k++) {
            hdr[0] = (unsigned char)(NODE ^ x); /* the switch routes on it */
            hdr[1] = (unsigned char)VL(0, x, k);        /* and deletes it */
            t9_vlink(VL(0, x, k), vlpkt[VL(0, x, k)], x, hdr, 2);
            hdr[0] = (unsigned char)(NODE ^ 4);
            hdr[1] = (unsigned char)VL(1, x, k);
            t9_vlink(VL(1, x, k), vlpkt[VL(1, x, k)], x, hdr, 2);
        }
#endif
    t9_vcp_start(NVL);
}

#if VXFER
static t9_xfer jobs[NJOBS];

static void add_job(int j, void *chan, void *buf, int bytes, int out)
{
    jobs[j].chan = chan;
    jobs[j].buf = buf;
    jobs[j].len = (unsigned)bytes;
    (void)out;
}

static void run_jobs(int n) { __vxfer(jobs, (unsigned)n); }
#else
/* Up to six transfers run at once, each a process of its own that takes
 * its job number from `job` and reports on a channel of its own: a channel
 * holds one waiting process, so two jobs ending on a shared one would
 * corrupt it. */
static struct { void *chan; char *buf; int bytes, out; } jobs[NJOBS];
static Channel job = NOPROCESS, done[NJOBS];
static int agws[NJOBS][256];

static void ag_worker(void)
{
    int j = ChanInInt(&job);

    if (jobs[j].out)
        __out(jobs[j].bytes, jobs[j].chan, jobs[j].buf);
    else
        __in(jobs[j].bytes, jobs[j].chan, jobs[j].buf);
    ChanOutInt(&done[j], j);
    PStop();
}

static void add_job(int j, void *chan, void *buf, int bytes, int out)
{
    jobs[j].chan = chan;
    jobs[j].buf = buf;
    jobs[j].bytes = bytes;
    jobs[j].out = out;
}

static void run_jobs(int n)
{
    int j;

    for (j = 0; j < n; j++) {
        done[j] = NOPROCESS;
        PRun(PSetup(agws[j], sizeof agws[j], ag_worker) | PRI_LOW);
        ChanOutInt(&job, j);
    }
    for (j = 0; j < n; j++)
        ChanInInt(&done[j]);
}
#endif

#if DIRECT
/* Bytes [off, off + bytes) of every piece of `len` floats: this node's go
 * to all seven peers and theirs come in, a third on each plane, as one
 * step.  7/3 piece times on each plane, the most three planes allow. */
static int exchange_list(t9_xfer *l, float *v, int len, int off, int bytes)
{
    int third = (bytes / 3) & ~3, d, q, n = 0;

    for (d = 1; d < P; d++) {
        int o = off;

        for (q = 1; q <= 3; q++) {
            int b = q < 3 ? third : bytes - 2 * third;

            l[n].chan = T9_VOUT(VLD(d, q));
            l[n].buf = (char *)(v + NODE * len) + o;
            l[n++].len = (unsigned)b;
            l[n].chan = T9_VIN(VLD(d, q));
            l[n].buf = (char *)(v + (NODE ^ d) * len) + o;
            l[n++].len = (unsigned)b;
            o += b;
        }
    }
    return n;
}

static void allgather_c104(float *v, int len)
{
    __vxfer(jobs, (unsigned)exchange_list(jobs, v, len, 0, len * 4));
}

#if OVERLAP
/* Compute and all-gather overlap: the compute does its piece in NCH chunks
 * and hands each to a high-priority process that exchanges it with every
 * peer while the next is computed - the VCP moves data beside the CPU. */
#ifndef NCH
#define NCH     4
#endif
/* Each chunk's transfer list is built once, as a layer loop would, and
 * reused: an input entry's length comes back unchanged when the whole
 * chunk arrives. */
static t9_xfer plist[2][NCH][NJOBS];
static int plen;
static Channel ready = NOPROCESS, gathered = NOPROCESS;
static int gws[1024], gsel;

static void gather_plan(int sel, float *v, int len)
{
    int c, cb = len * 4 / NCH;

    for (c = 0; c < NCH; c++)
        plen = exchange_list(plist[sel][c], v, len, c * cb, cb);
}

static void gatherer(void)
{
    int c;

    for (c = 0; c < NCH; c++) {
        ChanInInt(&ready);
        __vxfer(plist[gsel][c], (unsigned)plen);
    }
    ChanOutInt(&gathered, 0);
    PStop();
}

static void gather_start(int sel)
{
    gsel = sel;
    PRun(PSetup(gws, sizeof gws, gatherer) | PRI_HIGH);
}
#endif
#else
/* `bytes` from src to a peer and as many from it into dst, in SPLIT
 * word-aligned parts on that peer's virtual links of the plane */
static int add_split(int n, int kind, int plane, void *src, void *dst, int bytes)
{
    int k, off = 0, part = (bytes / SPLIT) & ~3;

    for (k = 0; k < SPLIT; k++) {
        int b = k < SPLIT - 1 ? part : bytes - off;
        add_job(n++, T9_VOUT(VL(kind, plane, k)), (char *)src + off, b, 1);
        add_job(n++, T9_VIN(VL(kind, plane, k)), (char *)dst + off, b, 0);
        off += b;
    }
    return n;
}

/* 1: the four of this group swap pieces, one peer per plane.  2: the
 * group's four pieces (contiguous) go to the partner group, a third on each
 * plane.  1 + 4/3 piece times in all, the most three planes allow for seven
 * pieces; recursive doubling on the hypercube takes 1 + 2 + 4. */
static void allgather_c104(float *v, int len)
{
    int g = NODE >> 2, bytes = 4 * len * 4, third = (bytes / 3) & ~3, off = 0;
    char *mine = (char *)(v + 4 * g * len), *theirs = (char *)(v + 4 * (g ^ 1) * len);
    int x, p, n = 0;

    for (x = 1; x <= 3; x++)
        n = add_split(n, 0, x, v + NODE * len, v + (NODE ^ x) * len, len * 4);
    run_jobs(n);
    n = 0;
    for (p = 1; p <= 3; p++) {
        int b = p < 3 ? third : bytes - 2 * third;
        n = add_split(n, 1, p, mine + off, theirs + off, b);
        off += b;
    }
    run_jobs(n);
}
#endif
#define ALLGATHER_ALL(v, len)   allgather_c104(v, len)
#else
#define ALLGATHER_ALL(v, len)   allgather(v, len, NODE, all3, 3)
#endif

int test_main(void)
{
    static const int all3[3] = { 0, 1, 2 }, col2[2] = { 1, 2 };
    int i, bad = 0, w = NODE;
    int r = NODE >> 1, c = NODE & 1;

    for (kfmt = 0; kfmt < QALL_N && qall_fmt[kfmt] != FMT; kfmt++)
        ;
    for (i = 0; i < D; i++)
        x[i] = (float)((i % 31) - 15) * 0.0078125f;
#if SCHEME == 1
    fill(Wg, 0, NODE * 288, 288, 0, NBD);
    fill(Wu, 1, NODE * 288, 288, 0, NBD);
    fill(Wd, 2, NODE * 640, 640, 0, NBF);
#else
    fill(Wg, 0, r * 576, 576, c * 10, 10);
    fill(Wu, 1, r * 576, 576, c * 10, 10);
    fill(Wd, 2, (c * 4 + r) * 640, 640, 0, NBF);
#endif
    __vsetfmt(0);
    __vsetlen(D);
    quant(xq, x);                          /* the token, already here */
#if C104
    c104_setup();
#if OVERLAP
    gather_plan(0, h, 288);
    gather_plan(1, y, 640);
#endif
#endif
    {                                       /* barrier: one timeline */
        float t[P];
        ALLGATHER_ALL(t, 1);
    }
    us("start\n");
#if OVERLAP
    {
        int ch, rows = 288 / NCH, r0;

        gather_start(0);
        for (ch = 0; ch < NCH; ch++) {
            r0 = NODE * 288 + ch * rows;
            gemm(g + r0, Wg + ch * rows * NBD * BSZ, xq, rows, NBD);
            gemm(u + r0, Wu + ch * rows * NBD * BSZ, xq, rows, NBD);
            __vsetlen(rows);
            __vsilu(g + r0, g + r0);
            __vmul(h + r0, g + r0, u + r0);
            ChanOutInt(&ready, ch);         /* exchanged while we go on */
        }
        us("gateup\n");
        ChanInInt(&gathered);
        us("h\n");
        __vsetlen(F);
        quant(hq, h);
        rows = 640 / NCH;
        gather_start(1);
        for (ch = 0; ch < NCH; ch++) {
            gemm(y + NODE * 640 + ch * rows, Wd + ch * rows * NBF * BSZ, hq, rows, NBF);
            ChanOutInt(&ready, ch);
        }
        us("down\n");
        ChanInInt(&gathered);
    }
#elif SCHEME == 1
    gemm(g + NODE * 288, Wg, xq, 288, NBD);
    gemm(u + NODE * 288, Wu, xq, 288, NBD);
    us("gateup\n");
#else
    gemm(gp, Wg, xq + c * 10 * 292, 576, 10);
    gemm(up_, Wu, xq + c * 10 * 292, 576, 10);
    us("gateup\n");
    /* reduce-scatter with the partner over dimension 0: keep rows c*288.. */
    __out(288 * 4, LINKOUT(1), gp + (1 - c) * 288);
    __out(288 * 4, LINKOUT(1), up_ + (1 - c) * 288);
    __in(288 * 4, LINKIN(1), tmp);
    __in(288 * 4, LINKIN(1), tmp + 288);
    __vsetlen(288);
    __vadd(g + NODE * 288, gp + c * 288, tmp);
    __vadd(u + NODE * 288, up_ + c * 288, tmp + 288);
    us("rs\n");
#endif
#if !OVERLAP
    __vsetlen(288);
    __vsilu(g + NODE * 288, g + NODE * 288);
    __vmul(h + NODE * 288, g + NODE * 288, u + NODE * 288);
    ALLGATHER_ALL(h, 288);
    us("h\n");
    __vsetlen(F);
    quant(hq, h);
#if SCHEME == 1
    gemm(y + NODE * 640, Wd, hq, 640, NBF);
    us("down\n");
    ALLGATHER_ALL(y, 640);                  /* the whole next input */
#else
    gemm(y + c * 2560 + r * 640, Wd, hq, 640, NBF);
    us("down\n");
    allgather(y + c * 2560, 640, r, col2, 2);   /* this column's half */
#endif
#endif
    us("done\n");
#if VERIFY
    /* node 0 alone, the same sums in the same order */
    fill(RgL, 0, 0, F, 0, 10); fill(RgR, 0, 0, F, 10, 10);
    fill(RuL, 1, 0, F, 0, 10); fill(RuR, 1, 0, F, 10, 10);
    fill(Rd, 2, 0, D, 0, NBF);
    us("loaded\n");
# if SCHEME == 1
    {
        static unsigned char RgF[F * NBD * BSZ], RuF[F * NBD * BSZ];
        fill(RgF, 0, 0, F, 0, NBD);
        fill(RuF, 1, 0, F, 0, NBD);
        gemm(rg2, RgF, xq, F, NBD);
        gemm(ru2, RuF, xq, F, NBD);
    }
# else
    gemm(gp, RgL, xq, F, 10); gemm(rg2, RgR, xq + 10 * 292, F, 10);
    __vsetlen(F); __vadd(rg2, gp, rg2);
    gemm(gp, RuL, xq, F, 10); gemm(ru2, RuR, xq + 10 * 292, F, 10);
    __vsetlen(F); __vadd(ru2, gp, ru2);
# endif
    __vsetlen(F);
    __vsilu(rg2, rg2);
    __vmul(tmp, rg2, ru2);
    quant(hq, tmp);
    gemm(ry, Rd, hq, D, NBF);
    for (i = 0; i < (SCHEME == 1 ? D : 2560); i++)
        if (f2b(ry[i]) != f2b(y[i]))
            bad++;
    us(bad ? "VERIFY BAD\n" : "VERIFY OK\n");
#endif
    (void)w;
    return bad;
}
