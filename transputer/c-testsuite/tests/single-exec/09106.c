/* VSETSTRIDE / FTCULL / TSKIN (OPR 0x1E9..0x1EB).
 *
 * Strided FTRANSPB must equal dense FTRANSPB on the same vertices;
 * FTCULL outcodes/distances against in-test C references (sign is the
 * raw IEEE bit, so -0.0 is "outside"); TSKIN against hand-computed
 * weighted joint transforms including the fused min/max mode.
 */
int printf(const char *, ...);

void __vsetlen(int n);
void __vsetstride(unsigned stride_bytes, unsigned stream_mask);
void __ftsetvp(const float *vp);
int __ftranspb(float *dst, const float *xyz, const float *mtx);
int __ftcull(unsigned char *dst, const float *xyz, const void *param);
int __tskin(float *dst, const void *joints, const void *param);

union pun { float f; unsigned u; };

static int feq(float a, float b, float tol)
{
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d <= tol;
}

static float vp[5] = { 24.0f, -11.0f, 32.0f, 15.0f, 1.0f };

static float mtx[16] = {
    1.0f, 0.0f,  0.0f, 0.0f,
    0.0f, 1.0f,  0.0f, 0.0f,
    0.0f, 0.0f,  1.0f, 0.0f,
    0.0f, 0.0f, -1.0f, 0.0f
};

/* 4 verts dense, and the same 4 padded to 20-byte (5-float) records */
static float dense[12] = {
    -1.0f, -1.0f, -3.0f,   1.0f, -1.0f, -3.0f,
     1.0f,  1.0f, -3.0f,  -1.0f,  1.0f, -3.0f
};
static float padded[20];

static float recs_a[4 * 8], recs_b[4 * 8];

int main(void)
{
    int i, bad;

    __ftsetvp(vp);
    __vsetlen(4);

    /* ---- VSETSTRIDE: strided source must equal dense source -------- */
    for (i = 0; i < 4; i++) {
        padded[i * 5 + 0] = dense[i * 3 + 0];
        padded[i * 5 + 1] = dense[i * 3 + 1];
        padded[i * 5 + 2] = dense[i * 3 + 2];
        padded[i * 5 + 3] = 99.0f;              /* padding, must be ignored */
        padded[i * 5 + 4] = -99.0f;
    }
    __ftranspb(recs_a, dense, mtx);
    __vsetstride(20u, 2u);                      /* B stream walks 20 bytes  */
    __ftranspb(recs_b, padded, mtx);
    __vsetstride(0u, 0u);                       /* back to dense            */
    bad = 0;
    for (i = 0; i < 4 * 8; i++)
        if (!feq(recs_a[i], recs_b[i], 0.0f))
            bad++;
    printf("vsetstride %s\n", bad == 0 ? "OK" : "BAD");

    /* ---- FTCULL ---------------------------------------------------- */
    {
        /* param: nplanes=2, mode=0, radius=0, xmask=0, planes:
         * x >= 0 halfspace (1,0,0,0) and x <= 2 halfspace (-1,0,0,2) */
        static struct {
            unsigned nplanes, mode;
            float radius;
            unsigned xmask;
            float planes[8];
        } prm = { 2u, 0u, 0.0f, 0u,
                  { 1.0f, 0.0f, 0.0f, 0.0f,   -1.0f, 0.0f, 0.0f, 2.0f } };
        static float pts[9] = {
            -1.0f, 0.0f, 0.0f,      /* left of both -> bit0            */
             0.5f, 0.0f, 0.0f,      /* inside       -> 0               */
             3.0f, 0.0f, 0.0f       /* right        -> bit1            */
        };
        static unsigned char codes[3];
        int orc;

        __vsetlen(3);
        orc = __ftcull(codes, pts, &prm);
        bad = 0;
        if (codes[0] != 1u) bad++;
        if (codes[1] != 0u) bad++;
        if (codes[2] != 2u) bad++;
        if (orc != 3) bad++;
        printf("ftcull codes %s\n", bad == 0 ? "OK" : "BAD");

        /* mode 2: raw distances to plane 0 */
        {
            static float dists[3];
            prm.mode = 2u;
            __ftcull((unsigned char *)dists, pts, &prm);
            bad = 0;
            if (!feq(dists[0], -1.0f, 0.0f)) bad++;
            if (!feq(dists[1], 0.5f, 0.0f)) bad++;
            if (!feq(dists[2], 3.0f, 0.0f)) bad++;
            printf("ftcull dist %s\n", bad == 0 ? "OK" : "BAD");
            prm.mode = 0u;
        }
    }

    /* ---- TSKIN ------------------------------------------------------ */
    {
        /* joint 0 = identity; joint 1 = translate (10, 20, 30) */
        static float joints[24] = {
            1.0f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 10.0f, 0.0f, 1.0f, 0.0f, 20.0f,
            0.0f, 0.0f, 1.0f, 30.0f
        };
        /* v0: 1 weight on joint 0; v1: 0.5/0.5 across both joints;
         * v2: 1 weight on joint 1.  Offsets are byte offsets (48/joint). */
        static float weights[16] = {
            2.0f, 3.0f, 4.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 0.5f,   1.0f, 1.0f, 1.0f, 0.5f,
            0.0f, 0.0f, 5.0f, 1.0f
        };
        static unsigned pairs[8] = { 0u, 1u,  0u, 0u, 48u, 1u,  48u, 1u };
        static struct {
            unsigned mode;
            float *weights;
            unsigned *pairs;
            float minmax[6];
        } prm;
        static float out[9];
        int wn;

        prm.mode = 1u;
        prm.weights = weights;
        prm.pairs = pairs;
        prm.minmax[0] = prm.minmax[1] = prm.minmax[2] = 1e30f;
        prm.minmax[3] = prm.minmax[4] = prm.minmax[5] = -1e30f;

        __vsetlen(3);
        wn = __tskin(out, joints, &prm);
        bad = 0;
        if (wn != 4) bad++;
        /* v0 = identity * (2,3,4,1) = (2,3,4) */
        if (!feq(out[0], 2.0f, 0.0f) || !feq(out[1], 3.0f, 0.0f) ||
            !feq(out[2], 4.0f, 0.0f)) bad++;
        /* v1 = I*(1,1,1,.5) + T*(1,1,1,.5) = (1,1,1)+(6,11,16) = (7,12,17) */
        if (!feq(out[3], 7.0f, 0.0f) || !feq(out[4], 12.0f, 0.0f) ||
            !feq(out[5], 17.0f, 0.0f)) bad++;
        /* v2 = translate * (0,0,5,1) = (10, 20, 35) */
        if (!feq(out[6], 10.0f, 0.0f) || !feq(out[7], 20.0f, 0.0f) ||
            !feq(out[8], 35.0f, 0.0f)) bad++;
        /* fused bounds */
        if (!feq(prm.minmax[0], 2.0f, 0.0f) ||
            !feq(prm.minmax[1], 3.0f, 0.0f) ||
            !feq(prm.minmax[2], 4.0f, 0.0f) ||
            !feq(prm.minmax[3], 10.0f, 0.0f) ||
            !feq(prm.minmax[4], 20.0f, 0.0f) ||
            !feq(prm.minmax[5], 35.0f, 0.0f)) bad++;
        printf("tskin %s\n", bad == 0 ? "OK" : "BAD");
    }

    return 0;
}
