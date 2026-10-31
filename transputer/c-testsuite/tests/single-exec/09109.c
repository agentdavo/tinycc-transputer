/* VFILL / VFILL2D / CSUM / VBLEND2D / STVXSTATE (OPR 0x1F1..0x1F5).
 *
 * Fill checks the (address & 3) pattern-byte rule at unaligned offsets
 * and odd lengths plus untouched sentinels; the 2D forms ride the
 * move2dinit BMReg latches; CSUM against hand-computed Internet-sum
 * vectors including end-around carry; STVXSTATE against every latch.
 */
int printf(const char *, ...);

void __vsetlen(int n);
void __vsetstride(unsigned stride_bytes, unsigned mask);
void __vsetfmt(unsigned fmt);
void __qsetfmt(int t);
void __ftsetvp(const float *vp);
void __ftsetlit(const float *lit);
void __move2dinit(int nrows, int dst_stride, int src_stride);
void __vfill(void *dst, unsigned pattern, unsigned nbytes);
void __vfill2d(void *dst, unsigned pattern, unsigned width_bytes);
unsigned __csumb(const void *src, unsigned init, unsigned nbytes);
unsigned __csumcpy(void *dst, const void *src, unsigned nbytes);
void __vblend8(unsigned *dst, const unsigned *src, unsigned ctl);
void __vblend2d(unsigned *dst, const unsigned *src, unsigned ctl);
void __stvxstate(void *save);

union pun { float f; unsigned u; };

static unsigned char pat_byte(unsigned pattern, unsigned k)
{
    return (unsigned char)((pattern >> ((k & 3u) * 8u)) & 0xFFu);
}

static unsigned blend_ref(unsigned d, unsigned s, unsigned ctl)
{
    unsigned a = (ctl > 0xFFu) ? (s >> 24) & 0xFFu : ctl;
    unsigned na = 255u - a, out = 0, sh;
    for (sh = 0; sh < 32; sh += 8) {
        unsigned t = ((s >> sh) & 0xFFu) * a + ((d >> sh) & 0xFFu) * na + 128u;
        out |= (((t + (t >> 8)) >> 8) & 0xFFu) << sh;
    }
    return out;
}

int main(void)
{
    int i, bad;

    /* ---- VFILL ------------------------------------------------------ */
    {
        static unsigned char buf[64];
        static unsigned wbuf[8];

        for (i = 0; i < 64; i++) buf[i] = 0xEE;
        __vfill(buf + 3, 0xA1B2C3D4u, 9u);
        bad = 0;
        for (i = 0; i < 64; i++) {
            unsigned char want = (i >= 3 && i < 12)
                ? pat_byte(0xA1B2C3D4u, (unsigned)i) : 0xEE;
            if (buf[i] != want) bad++;
        }
        __vfill(buf, 0x55555555u, 64u);
        for (i = 0; i < 64; i++)
            if (buf[i] != 0x55) bad++;
        __vfill(wbuf, 0x11223344u, 32u);
        for (i = 0; i < 8; i++)
            if (wbuf[i] != 0x11223344u) bad++;
        printf("vfill %s\n", bad == 0 ? "OK" : "BAD");
    }

    /* ---- VFILL2D ---------------------------------------------------- */
    {
        static unsigned char b2[64];

        for (i = 0; i < 64; i++) b2[i] = 0xEE;
        __move2dinit(3, 16, 0);          /* 3 rows, dst stride 16 */
        __vfill2d(b2 + 2, 0x00FF00FFu, 8u);
        bad = 0;
        for (i = 0; i < 64; i++) {
            int row = i / 16, col = i % 16;
            unsigned char want = (row < 3 && col >= 2 && col < 10)
                ? pat_byte(0x00FF00FFu, (unsigned)i) : 0xEE;
            if (b2[i] != want) bad++;
        }
        printf("vfill2d %s\n", bad == 0 ? "OK" : "BAD");
    }

    /* ---- CSUM ------------------------------------------------------- */
    {
        static unsigned char d[6] = { 1, 2, 3, 4, 5, 6 };
        static unsigned char w[4] = { 0xFF, 0xFF, 0x02, 0x00 };

        bad = 0;
        if (__csumb(d, 0u, 6u) != 0x0C09u) bad++;
        if (__csumb(d, 0u, 5u) != 0x0609u) bad++;
        if (__csumb(d, 0x1FFFFu, 0u) != 1u) bad++;       /* pure fold  */
        if (__csumb(w, 0u, 4u) != 2u) bad++;             /* wrap carry */
        printf("csum %s\n", bad == 0 ? "OK" : "BAD");

        /* fused copy+checksum: bytes copied, sum equals plain CSUM,
         * odd length and unaligned destination included */
        {
            static unsigned char cp[8];
            unsigned s;
            for (i = 0; i < 8; i++) cp[i] = 0xAA;
            s = __csumcpy(cp + 1, d, 5u);
            bad = 0;
            if (s != __csumb(d, 0u, 5u)) bad++;
            for (i = 0; i < 5; i++)
                if (cp[1 + i] != d[i]) bad++;
            if (cp[0] != 0xAA || cp[6] != 0xAA || cp[7] != 0xAA) bad++;
            printf("csumcpy %s\n", bad == 0 ? "OK" : "BAD");
        }
    }

    /* ---- VBLEND2D --------------------------------------------------- */
    {
        static unsigned dst[8], src[6], keep[8];

        for (i = 0; i < 8; i++) dst[i] = 0x11000000u * (unsigned)(i + 1);
        for (i = 0; i < 6; i++) src[i] = 0x80402010u + 0x01010101u * (unsigned)i;
        for (i = 0; i < 8; i++) keep[i] = dst[i];

        __move2dinit(2, 16, 12);         /* 2 rows, dst 16, src 12 */
        __vsetlen(3);
        __vblend2d(dst, src, 96u);

        bad = 0;
        for (i = 0; i < 8; i++) {
            int row = i / 4, col = i % 4;
            unsigned want = (col < 3)
                ? blend_ref(keep[i], src[row * 3 + col], 96u)
                : keep[i];
            if (dst[i] != want) bad++;
        }
        printf("vblend2d %s\n", bad == 0 ? "OK" : "BAD");
    }

    /* ---- STVXSTATE -------------------------------------------------- */
    {
        static float vp2[5] = { 2.0f, 3.0f, 4.0f, 5.0f, 1.5f };
        static float lt2[5] = { 0.0f, 1.0f, 0.0f, 0.25f, 0.5f };
        static unsigned save[15];
        static float fwant[10] = {
            2.0f, 3.0f, 4.0f, 5.0f, 1.5f,
            0.0f, 1.0f, 0.0f, 0.25f, 0.5f
        };
        union pun p;

        __vsetlen(7);
        __qsetfmt(8);
        __vsetstride(20u, 2u);
        __vsetfmt(1u);
        __ftsetvp(vp2);
        __ftsetlit(lt2);
        __stvxstate(save);
        __vsetstride(0u, 0u);
        __vsetfmt(0u);

        bad = 0;
        if (save[0] != 7u) bad++;
        if (save[1] != 8u) bad++;
        if (save[2] != 20u) bad++;
        if (save[3] != 2u) bad++;
        if (save[4] != 1u) bad++;
        for (i = 0; i < 10; i++) {
            p.f = fwant[i];
            if (save[5 + i] != p.u) bad++;
        }
        printf("stvxstate %s\n", bad == 0 ? "OK" : "BAD");
    }

    return 0;
}
