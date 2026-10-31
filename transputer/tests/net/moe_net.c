/* moe_net.c - one MoE layer of DeepSeek-V4.1-Flash for one token, across a
 * network of seven T4 nodes joined by links:
 *
 *        root 0 --link1-- 1 --link2-- 4
 *               --link2-- 2 --link2-- 5        (each node's link 1 is its
 *               --link3-- 3 --link2-- 6         parent; link 0 is its host)
 *
 * The root quantises the token's activations to Q8_K (VQ8K) and sends them
 * down the tree; nodes 1-3 pass them on to their child.  Every node 1-6
 * holds one real-size routed expert (IQ1_S, real blocks cycled) and runs it
 * on the integer path, scales its output by its router weight and sends it
 * back up; nodes 1-3 forward their child's after their own.  The root sums
 * the six.
 *
 * Built once per node with -DNODE=n.  With -DVERIFY the root then runs the
 * same six experts itself, in the same order, which both checks the network
 * result bit for bit and times the single-node baseline.  Each phase prints
 * a line, so T4_T9K_MARKS=1 timestamps it; T4_T9K_NET puts every node on
 * one timeline (t4/t9000_group.h). */
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
void __vfill(void *dst, unsigned pattern, unsigned nbytes);
void __vcvt(void *dst, const void *src, unsigned dst_fmt);
void *memcpy(void *d, const void *s, unsigned n);

#define FMT 19                          /* iq1_s */
#define BSZ 50
#define D   5120
#define F   2304
#define NBD (D / 256)
#define NBF (F / 256)
#define XQ  (NBD * 292)                 /* the token's Q8_K activations */
/* CHUNK=1 forwards a piece at a time (CH_X bytes of the token, CH_Y floats
 * of an output) instead of whole messages, and REDUCE=1 (which needs CHUNK)
 * has nodes 1-3 add their child's output to their own on the way up. */
#ifndef CHUNK
#define CHUNK 0
#endif
#ifndef REDUCE
#define REDUCE 0
#endif
/* XF32=1 broadcasts the token as 20 KB of f32 (always cut-through) and has
 * every node quantise it, instead of sending the root's 5.8 KB of Q8_K. */
#ifndef XF32
#define XF32 0
#endif
/* YBF16=1 (with CHUNK and REDUCE) sends outputs and partial sums up the
 * tree as bf16 (VCVT), widening each piece before it is added. */
#ifndef YBF16
#define YBF16 0
#endif
#if YBF16 && !(CHUNK && REDUCE)
#error YBF16 needs CHUNK=1 and REDUCE=1
#endif
#ifndef CH_X
#define CH_X 1024
#endif
#ifndef CH_Y
#define CH_Y 256
#endif

#if NODE != 0 || VERIFY
static unsigned char Wg[F * NBD * BSZ], Wu[F * NBD * BSZ], Wd[D * NBF * BSZ];
#endif
static float x[D], g[F], u[F], h[F], y[D], y2[D], acc[D], tmp[D];
static unsigned short ybf[D], cbf[D];       /* YBF16: on the wire */

/* v = f32(bf16(v)): what a vector looks like after crossing a link as bf16 */
static void bf16_round(float *v, int n)
{
    __vsetlen(n);
    __vsetfmt(0);
    __vcvt(ybf, v, 1);
    __vsetfmt(1);
    __vcvt(v, ybf, 0);
    __vsetfmt(0);
}
static unsigned char xq[XQ], hq[NBF * 292];
static struct { const void *acts; const void *weights; } prm;
static int kfmt;

#if NODE != 0 || VERIFY
/* Expert e's weights: the real blocks in an order of its own. */
static void fill(unsigned char *w, int nblocks, int seed)
{
    int b;
    for (b = 0; b < nblocks; b++)
        memcpy(w + b * BSZ,
               qall_blk[kfmt] + ((b * (2 * seed + 1) + seed) % QALL_NB) * BSZ, BSZ);
}

static void load_expert(int e)
{
    fill(Wg, F * NBD, e);
    fill(Wu, F * NBD, e + 1);
    fill(Wd, D * NBF, e + 2);
}

/* y = router_weight(e) * down(silu(gate x) * up x), x already in xq */
static void expert(int e, float *out)
{
    __qsetfmt(FMT);
    __vsetfmt(3);
    __vsetlen(F);
    prm.acts = xq;
    prm.weights = Wg; __qgemm(g, &prm, NBD);
    prm.weights = Wu; __qgemm(u, &prm, NBD);
    __vsilu(g, g);
    __vmul(h, g, u);
    __vsetfmt(0);
    __vsetlen(F);
    __vq8k(hq, h);
    __vsetfmt(3);
    __vsetlen(D);
    prm.acts = hq;
    prm.weights = Wd; __qgemm(out, &prm, NBF);
    __vfill(tmp, f2b(0.05f * (float)e), D * 4);
    __vmul(out, out, tmp);
    __vsetfmt(0);
}
#endif

int test_main(void)
{
    int i, l, k, n, off, w = 1, bad = 0;

    for (kfmt = 0; kfmt < QALL_N && qall_fmt[kfmt] != FMT; kfmt++)
        ;
#if NODE == 0
    for (i = 0; i < D; i++)
        x[i] = (float)((i % 31) - 15) * 0.0078125f;
    for (l = 1; l <= 3; l++)                /* every subtree is loaded */
        __in(4, LINKIN(l), &w);
    us("start\n");
    __vsetfmt(0);
    __vsetlen(D);
    __vq8k(xq, x);
    for (l = 1; l <= 3; l++)
        if (XF32)                           /* f32 on the wire */
            for (off = 0; off < D; off += CH_Y) {
                n = D - off < CH_Y ? D - off : CH_Y;
                __out(n * 4, LINKOUT(l), x + off);
            }
        else
            __out(XQ, LINKOUT(l), xq);
    __vfill(acc, 0, D * 4);
    __vsetlen(D);
    for (l = 1; l <= 3; l++)
        for (k = 0; k < (REDUCE ? 1 : 2); k++) {
            /* the subtree's sum, or the child's own and then its child's */
            if (YBF16) {
                __in(D * 2, LINKIN(l), ybf);
                __vsetlen(D);
                __vsetfmt(1);
                __vcvt(y, ybf, 0);
                __vsetfmt(0);
            } else
                __in(D * 4, LINKIN(l), y);
            __vsetlen(D);
            __vadd(acc, acc, y);
        }
    us("layer\n");
# if VERIFY
    {
        static const int order[6] = { 1, 4, 2, 5, 3, 6 };
        __vfill(x, 0, D * 4);               /* x becomes the reference sum */
        for (k = 0; k < 6; k++) {
            load_expert(order[k]);
            us("loaded\n");
            expert(order[k], (k & 1) ? y2 : y);
            us("computed\n");
            __vsetlen(D);
#  if REDUCE
            if (k & 1) {                    /* in the order node 1-3 adds */
                if (YBF16)                  /* the child's crossed a link */
                    bf16_round(y2, D);
                __vsetlen(D);
                __vadd(y, y, y2);
                if (YBF16)                  /* and so did the sum */
                    bf16_round(y, D);
                __vsetlen(D);
                __vadd(x, x, y);
            }
#  else
            __vadd(x, x, (k & 1) ? y2 : y);
#  endif
        }
        for (i = 0; i < D; i++)
            if (f2b(x[i]) != f2b(acc[i]))
                bad++;
        us(bad ? "VERIFY BAD\n" : "VERIFY OK\n");
    }
# endif
#else
    load_expert(NODE);
    if (NODE <= 3)
        __in(4, LINKIN(2), &w);             /* child loaded */
    __out(4, LINKOUT(1), &w);
    if (XF32) {                             /* f32 token, quantised here */
        for (off = 0; off < D; off += CH_Y) {
            n = D - off < CH_Y ? D - off : CH_Y;
            __in(n * 4, LINKIN(1), x + off);
            if (NODE <= 3)
                __out(n * 4, LINKOUT(2), x + off);
        }
        __vsetfmt(0);
        __vsetlen(D);
        __vq8k(xq, x);
    } else if (CHUNK && NODE <= 3)
        /* cut-through: pass each packet on as it arrives */
        for (off = 0; off < XQ; off += CH_X) {
            n = XQ - off < CH_X ? XQ - off : CH_X;
            __in(n, LINKIN(1), xq + off);
            __out(n, LINKOUT(2), xq + off);
        }
    else {
        __in(XQ, LINKIN(1), xq);
        if (NODE <= 3)
            __out(XQ, LINKOUT(2), xq);
    }
    us("x\n");
    expert(NODE, y);
    us("expert\n");
    if (!CHUNK) {
        __out(D * 4, LINKOUT(1), y);
        if (NODE <= 3) {
            __in(D * 4, LINKIN(2), acc);
            __out(D * 4, LINKOUT(1), acc);
        }
    } else if (YBF16) {
        __vsetlen(D);
        __vcvt(ybf, y, 1);                  /* the leaves' whole output */
        for (off = 0; off < D; off += CH_Y) {
            n = D - off < CH_Y ? D - off : CH_Y;
            if (NODE <= 3) {                /* widen, add, narrow, pass on */
                __in(n * 2, LINKIN(2), cbf + off);
                __vsetlen(n);
                __vsetfmt(1);
                __vcvt(acc + off, cbf + off, 0);
                __vsetfmt(0);
                __vadd(y + off, y + off, acc + off);
                __vcvt(ybf + off, y + off, 1);
            }
            __out(n * 2, LINKOUT(1), ybf + off);
        }
    } else {
        if (NODE > 3 || !REDUCE)
            for (off = 0; off < D; off += CH_Y) {
                n = D - off < CH_Y ? D - off : CH_Y;
                __out(n * 4, LINKOUT(1), y + off);
            }
        if (NODE <= 3)
            for (off = 0; off < D; off += CH_Y) {
                n = D - off < CH_Y ? D - off : CH_Y;
                __in(n * 4, LINKIN(2), acc + off);
                if (REDUCE) {               /* add the child's as it arrives */
                    __vsetlen(n);
                    __vadd(y + off, y + off, acc + off);
                    __out(n * 4, LINKOUT(1), y + off);
                } else
                    __out(n * 4, LINKOUT(1), acc + off);
            }
    }
    us("sent\n");
#endif
    return bad;
}
