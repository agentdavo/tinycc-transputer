/* FTSETLIT / FLITB / QGEMM / VBLEND8 (OPR 0x1ED..0x1F0).
 *
 * FLITB against hand-derived intensities including back-face clamp,
 * unnormalised normals (FRSQRT path) and the all-dark return; QGEMM
 * bit-exact against a per-row __qdot loop (same decode + partial-sum
 * order), in both f32 and bf16 activation formats; VBLEND8 against an
 * in-test integer reference of the exact-/255 blend.
 */
int printf(const char *, ...);

void __vsetlen(int n);
void __vsetfmt(unsigned fmt);
void __qsetfmt(int ggml_type);
void __ftsetlit(const float *lit);
int __flitb(float *records, const float *normals, const float *mtx3x3);
int __qgemm(float *dst, const void *param, unsigned nblocks);
void __vblend8(unsigned *dst, const unsigned *src, unsigned alpha_ctl);
float __qdot(const void *blocks, const float *acts, int nblocks, float acc);

union pun { float f; unsigned u; };

static int feq(float a, float b, float tol)
{
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d <= tol;
}

static float ident3[9] = {
    1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f
};

int main(void)
{
    int i, bad;

    /* ---- FLITB ------------------------------------------------------ */
    {
        static float lit[5] = { 0.0f, 0.0f, 1.0f, 0.2f, 0.8f };
        static float normals[12] = {
            0.0f, 0.0f, 1.0f,       /* facing light  -> 1.0            */
            0.0f, 0.0f, -1.0f,      /* back face     -> ambient 0.2    */
            0.0f, 3.0f, 4.0f,       /* unnormalised: d = 4/5 -> 0.84   */
            0.0f, 0.0f, 0.0f        /* degenerate    -> ambient 0.2    */
        };
        static float recs[4 * 8];
        int dark;

        for (i = 0; i < 4 * 8; i++)
            recs[i] = 7.0f;
        __ftsetlit(lit);
        __vsetlen(4);
        dark = __flitb(recs, normals, ident3);

        bad = 0;
        if (dark != 0) bad++;
        if (!feq(recs[0 * 8 + 5], 1.0f, 1e-6f)) bad++;
        if (!feq(recs[1 * 8 + 5], 0.2f, 1e-6f)) bad++;
        if (!feq(recs[2 * 8 + 5], 0.84f, 1e-4f)) bad++;
        if (!feq(recs[3 * 8 + 5], 0.2f, 1e-6f)) bad++;
        /* every other record word must be untouched */
        for (i = 0; i < 4 * 8; i++)
            if (i % 8 != 5 && !feq(recs[i], 7.0f, 0.0f))
                bad++;
        printf("flitb %s\n", bad == 0 ? "OK" : "BAD");

        /* all facing away -> all_dark = 1 */
        {
            static float away[6] = {
                0.0f, 0.0f, -1.0f,   0.5f, 0.0f, -2.0f
            };
            __vsetlen(2);
            dark = __flitb(recs, away, ident3);
            printf("flitb dark %s\n", dark == 1 ? "OK" : "BAD");
        }
    }

    /* ---- QGEMM (Q8_0) ----------------------------------------------- */
    {
        /* 2 rows x 2 blocks of Q8_0: 2-byte f16 scale + 32 int8 */
        static unsigned char w[2 * 2 * 34];
        static float acts[64];
        static float out[2];
        static struct { float *acts; void *weights; } prm;
        float ref;
        int r;

        for (i = 0; i < 2 * 2 * 34; i++) {
            int off = i % 34;
            if (off == 0)      w[i] = 0x00;         /* f16 1.0 lo */
            else if (off == 1) w[i] = 0x3C;         /* f16 1.0 hi */
            else               w[i] = (unsigned char)(char)((i * 3) % 100 - 50);
        }
        for (i = 0; i < 64; i++)
            acts[i] = 0.25f * (float)i - 2.0f;

        __qsetfmt(8);
        prm.acts = acts;
        prm.weights = w;
        __vsetlen(2);
        r = __qgemm(out, &prm, 2u);

        bad = 0;
        if (r != 2) bad++;
        for (i = 0; i < 2; i++) {
            ref = __qdot(&w[i * 2 * 34], acts, 2, 0.0f);
            if (!feq(out[i], ref, 0.0f)) bad++;
        }
        printf("qgemm f32 %s\n", bad == 0 ? "OK" : "BAD");

        /* bf16 activations: reference is __qdot on the widened values */
        {
            static unsigned short a16[64];
            static float aw[64];
            union pun p;

            for (i = 0; i < 64; i++) {
                p.f = acts[i];
                a16[i] = (unsigned short)(p.u >> 16);
                p.u = (unsigned)a16[i] << 16;
                aw[i] = p.f;
            }
            prm.acts = (float *)a16;
            __vsetfmt(1u);
            __vsetlen(2);
            r = __qgemm(out, &prm, 2u);
            __vsetfmt(0u);

            bad = 0;
            if (r != 2) bad++;
            for (i = 0; i < 2; i++) {
                ref = __qdot(&w[i * 2 * 34], aw, 2, 0.0f);
                if (!feq(out[i], ref, 0.0f)) bad++;
            }
            printf("qgemm bf16 %s\n", bad == 0 ? "OK" : "BAD");
        }
    }

    /* ---- VBLEND8 ---------------------------------------------------- */
    {
        static unsigned dst[4] = {
            0xFF102030u, 0xFF102030u, 0x00FFFFFFu, 0x12345678u
        };
        static unsigned src[4] = {
            0x80FFFFFFu, 0x80FFFFFFu, 0xFF000000u, 0x9ABCDEF0u
        };
        unsigned keep[4], ctl;
        int c;

        for (i = 0; i < 4; i++) keep[i] = dst[i];

        for (c = 0; c < 3; c++) {
            ctl = (c == 0) ? 255u : (c == 1) ? 96u : 0xFFFFFFFFu;
            for (i = 0; i < 4; i++) dst[i] = keep[i];
            __vsetlen(4);
            __vblend8(dst, src, ctl);
            bad = 0;
            for (i = 0; i < 4; i++) {
                unsigned s = src[i], d = keep[i], out = 0, sh;
                unsigned a = (ctl > 0xFFu) ? (s >> 24) & 0xFFu : ctl;
                unsigned na = 255u - a;
                for (sh = 0; sh < 32; sh += 8) {
                    unsigned t = ((s >> sh) & 0xFFu) * a
                               + ((d >> sh) & 0xFFu) * na + 128u;
                    out |= (((t + (t >> 8)) >> 8) & 0xFFu) << sh;
                }
                if (dst[i] != out) bad++;
            }
            printf("vblend8 ctl%d %s\n", c, bad == 0 ? "OK" : "BAD");
        }
    }

    return 0;
}
