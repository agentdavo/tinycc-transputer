/* TVX-GFX group (OPR 0x1E2..0x1E8, 0x1EC): FTSETVP / FTRANSP / FTRANSPB /
 * FSINCOS / FRSQRT / FRECIP / FSINCOSB / VSETFMT against pure-C
 * references.  Outcodes must match exactly; screen coordinates and trig
 * values are checked to a tolerance since the reference path runs
 * through the emulated FPU's own instruction sequence.  (Lives at 09107
 * because 09104 was reused for an unrelated regression.)
 */
int printf(const char *, ...);

void __vsetlen(int n);
void __ftsetvp(const float *vp);
int __ftransp(float *dst, const float *xyz, const float *mtx);
int __ftranspb(float *dst, const float *xyz, const float *mtx);
void __fsincos(float *dst, unsigned bams);
unsigned __frsqrtb(unsigned bits);
unsigned __frecipb(unsigned bits);
void __fsincosb(float *dst, const unsigned short *bams);
void __vsetfmt(unsigned fmt);

union pun { float f; unsigned u; };

static int feq(float a, float b, float tol)
{
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d <= tol;
}

/* Reference: same arithmetic as tvx_transp in transputer/t4/tvx_gfx.h */
static unsigned ref_transp(float *dst, const float *s, const float *m,
                           const float *vp)
{
    float x = s[0], y = s[1], z = s[2];
    float xc = m[0]*x  + m[1]*y  + m[2]*z  + m[3];
    float yc = m[4]*x  + m[5]*y  + m[6]*z  + m[7];
    float zc = m[8]*x  + m[9]*y  + m[10]*z + m[11];
    float wc = m[12]*x + m[13]*y + m[14]*z + m[15];
    float g = vp[4] > 1.0f ? vp[4] : 1.0f;
    float gw = g * wc;
    unsigned code = 0;

    if (xc < -gw) code |= 0x01;
    if (xc >  gw) code |= 0x02;
    if (yc < -gw) code |= 0x04;
    if (yc >  gw) code |= 0x08;
    if (zc < -gw) code |= 0x10;
    if (zc >  gw) code |= 0x20;

    if (wc >= 1e-6f) {
        float iw = 1.0f / wc;
        dst[0] = (xc * iw) * vp[0] + vp[2];
        dst[1] = (yc * iw) * vp[1] + vp[3];
        dst[2] = zc * iw;
        dst[3] = iw;
    } else {
        code |= 0x10;
        dst[0] = dst[1] = dst[2] = dst[3] = 0.0f;
    }
    return code;
}

static float cube[24] = {
    -1.0f, -1.0f, -2.0f,   1.0f, -1.0f, -2.0f,
     1.0f,  1.0f, -2.0f,  -1.0f,  1.0f, -2.0f,
    -1.0f, -1.0f, -4.0f,   1.0f, -1.0f, -4.0f,
     1.0f,  1.0f, -4.0f,  -1.0f,  1.0f, -4.0f
};

/* camera at origin looking down -z: Wc = -z */
static float mtx[16] = {
    1.0f, 0.0f,  0.0f, 0.0f,
    0.0f, 1.0f,  0.0f, 0.0f,
    0.0f, 0.0f,  1.0f, 0.0f,
    0.0f, 0.0f, -1.0f, 0.0f
};

static float vp[5] = { 80.0f, -40.0f, 80.0f, 40.0f, 1.0f };

static float recs[8 * 8];
static float ref[4];

int main(void)
{
    int i, bad = 0;
    unsigned andc;
    float one[8];
    float sc[2];
    union pun p, q;

    __ftsetvp(vp);
    __vsetlen(8);

    /* block transform: outcodes exact, coords close, AND-code zero */
    andc = __ftranspb(recs, cube, mtx);
    for (i = 0; i < 8; i++) {
        unsigned rc = ref_transp(ref, &cube[i * 3], mtx, vp);
        union pun oc;
        oc.f = 0.0f;
        /* outcode word is integer bits in record word 4 */
        oc.u = ((unsigned *)recs)[i * 8 + 4];
        if (oc.u != rc) { printf("v%d outcode %u want %u\n", i, oc.u, rc); bad++; }
        if (!feq(recs[i * 8 + 0], ref[0], 0.01f) ||
            !feq(recs[i * 8 + 1], ref[1], 0.01f) ||
            !feq(recs[i * 8 + 2], ref[2], 0.001f) ||
            !feq(recs[i * 8 + 3], ref[3], 0.001f)) {
            printf("v%d coords off\n", i); bad++;
        }
    }
    printf("ftranspb andcode %u %s\n", andc, (andc == 0 && bad == 0) ? "OK" : "BAD");

    /* single-vertex form must equal record 0 of the block form */
    andc = __ftransp(one, cube, mtx);
    if (andc != ((unsigned *)recs)[4] ||
        !feq(one[0], recs[0], 0.0f) || !feq(one[1], recs[1], 0.0f) ||
        !feq(one[2], recs[2], 0.0f) || !feq(one[3], recs[3], 0.0f))
        printf("ftransp single BAD\n");
    else
        printf("ftransp single OK\n");

    /* trivially-rejectable batch: push cube 10 units right -> all RIGHT */
    for (i = 0; i < 8; i++) cube[i * 3] += 10.0f;
    andc = __ftranspb(recs, cube, mtx);
    printf("reject andcode %u %s\n", andc, (andc & 0x02) ? "OK" : "BAD");
    for (i = 0; i < 8; i++) cube[i * 3] -= 10.0f;

    /* VSETFMT: cube coords are powers of two, so bf16 positions must
     * reproduce the f32 records bit-for-bit */
    {
        static unsigned short c16[24];
        static float recs16[8 * 8];
        __ftranspb(recs, cube, mtx);
        for (i = 0; i < 24; i++) {
            p.f = cube[i];
            c16[i] = (unsigned short)(p.u >> 16);
        }
        __vsetfmt(1u);
        andc = __ftranspb(recs16, (const float *)c16, mtx);
        __vsetfmt(0u);
        bad = 0;
        for (i = 0; i < 8 * 8; i++)
            if (!feq(recs[i], recs16[i], 0.0f))
                bad++;
        printf("vsetfmt %s\n", (bad == 0 && andc == 0) ? "OK" : "BAD");
    }

    /* FSINCOS at the cardinal and half-quadrant points */
    bad = 0;
    __fsincos(sc, 0x0000u);
    if (!feq(sc[0], 0.0f, 1e-6f) || !feq(sc[1], 1.0f, 1e-6f)) bad++;
    __fsincos(sc, 0x2000u);
    if (!feq(sc[0], 0.70710678f, 1e-6f) || !feq(sc[1], 0.70710678f, 1e-6f)) bad++;
    __fsincos(sc, 0x4000u);
    if (!feq(sc[0], 1.0f, 1e-6f) || !feq(sc[1], 0.0f, 1e-6f)) bad++;
    __fsincos(sc, 0x8000u);
    if (!feq(sc[0], 0.0f, 1e-6f) || !feq(sc[1], -1.0f, 1e-6f)) bad++;
    __fsincos(sc, 0xC000u);
    if (!feq(sc[0], -1.0f, 1e-6f) || !feq(sc[1], 0.0f, 1e-6f)) bad++;
    __fsincos(sc, 0x1555u);     /* 30 degrees */
    if (!feq(sc[0], 0.5f, 1e-4f) || !feq(sc[1], 0.86602540f, 1e-4f)) bad++;
    printf("fsincos %s\n", bad == 0 ? "OK" : "BAD");

    /* FRSQRT: values and total domain edges */
    bad = 0;
    p.f = 1.0f;  q.u = __frsqrtb(p.u); if (!feq(q.f, 1.0f, 1e-5f)) bad++;
    p.f = 4.0f;  q.u = __frsqrtb(p.u); if (!feq(q.f, 0.5f, 1e-5f)) bad++;
    p.f = 0.25f; q.u = __frsqrtb(p.u); if (!feq(q.f, 2.0f, 1e-5f)) bad++;
    p.f = 2.0f;  q.u = __frsqrtb(p.u); if (!feq(q.f, 0.70710678f, 1e-5f)) bad++;
    if (__frsqrtb(0x00000000u) != 0x7FC00000u) bad++;   /* +0    -> qNaN */
    if (__frsqrtb(0x80000000u) != 0x7FC00000u) bad++;   /* -0    -> qNaN */
    if (__frsqrtb(0xBF800000u) != 0x7FC00000u) bad++;   /* -1.0  -> qNaN */
    if (__frsqrtb(0x7F800000u) != 0x00000000u) bad++;   /* +inf  -> +0   */
    printf("frsqrt %s\n", bad == 0 ? "OK" : "BAD");

    /* FRECIP: values, sign, and total domain edges */
    bad = 0;
    p.f = 2.0f;  q.u = __frecipb(p.u); if (!feq(q.f, 0.5f, 1e-6f)) bad++;
    p.f = 3.0f;  q.u = __frecipb(p.u); if (!feq(q.f, 0.33333334f, 1e-6f)) bad++;
    p.f = -4.0f; q.u = __frecipb(p.u); if (!feq(q.f, -0.25f, 1e-6f)) bad++;
    p.f = 0.1f;  q.u = __frecipb(p.u); if (!feq(q.f, 10.0f, 1e-4f)) bad++;
    if (__frecipb(0x00000000u) != 0x7F800000u) bad++;   /* +0   -> +inf */
    if (__frecipb(0x80000000u) != 0xFF800000u) bad++;   /* -0   -> -inf */
    if (__frecipb(0x7F800000u) != 0x00000000u) bad++;   /* +inf -> +0   */
    if (__frecipb(0xFF800000u) != 0x80000000u) bad++;   /* -inf -> -0   */
    if (__frecipb(0x7FC00001u) != 0x7FC00000u) bad++;   /* NaN  -> qNaN */
    printf("frecip %s\n", bad == 0 ? "OK" : "BAD");

    /* FSINCOSB must match the scalar FSINCOS bit-for-bit */
    {
        static unsigned short angs[4] = { 0x0000u, 0x2000u, 0x8CD1u, 0xF123u };
        static float pairs[8];
        bad = 0;
        __vsetlen(4);
        __fsincosb(pairs, angs);
        for (i = 0; i < 4; i++) {
            __fsincos(sc, angs[i]);
            if (!feq(pairs[i * 2], sc[0], 0.0f) ||
                !feq(pairs[i * 2 + 1], sc[1], 0.0f))
                bad++;
        }
        printf("fsincosb %s\n", bad == 0 ? "OK" : "BAD");
    }

    return 0;
}
