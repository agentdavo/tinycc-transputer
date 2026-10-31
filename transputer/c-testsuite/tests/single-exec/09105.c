/* Spinning cube on the TVX-GFX geometry group.
 *
 * Per frame: build a rotation+perspective matrix with FSINCOS, transform
 * all 8 vertices with one FTRANSPB, wireframe the 12 edges into an ASCII
 * framebuffer.  The AND-outcode from FTRANSPB must stay 0 (cube always
 * on screen), which is checked every frame.
 *
 * Compiled with -DHOST_MIRROR (plus -I transputer/t4) the same file runs
 * on the host against the tvx_gfx.h semantic kernels and prints identical
 * frames; that is how the .expected file is generated, so this test also
 * pins emulator == semantic-core bit-exactness through a whole pipeline.
 */
int printf(const char *, ...);

#ifdef HOST_MIRROR
#include "tvx_gfx.h"
static tvx_viewport vpst = { 1.0f, 1.0f, 0.0f, 0.0f, 1.0f };
static unsigned veclen;
static void __vsetlen(int n) { veclen = n; }
static void __ftsetvp(const float *d) { tvx_viewport_set(&vpst, d); }
static int __ftranspb(float *dst, const float *src, const float *m)
{
    unsigned i, andc = 0x3Fu;
    for (i = 0; i < veclen; i++) {
        unsigned c = tvx_transp(dst + i * 8, src + i * 3, m, &vpst);
        ((unsigned *)dst)[i * 8 + 4] = c;
        andc &= c;
    }
    return andc;
}
static void __fsincos(float *d, unsigned bams) { tvx_sincos(bams, d, d + 1); }
#else
void __vsetlen(int n);
void __ftsetvp(const float *vp);
int __ftranspb(float *dst, const float *xyz, const float *mtx);
void __fsincos(float *dst, unsigned bams);
#endif

#define W 64
#define H 30

static float cube[24] = {
    -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f
};

static int edges[24] = {
    0,1, 1,2, 2,3, 3,0,  4,5, 5,6, 6,7, 7,4,  0,4, 1,5, 2,6, 3,7
};

static float vp[5] = { 24.0f, -11.0f, 32.0f, 15.0f, 1.0f };

static float recs[8 * 8];
static char fb[H][W + 1];

static void plot(int x, int y)
{
    if (x >= 0 && x < W && y >= 0 && y < H)
        fb[y][x] = '#';
}

static void line(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0, sx = 1, sy = 1, err, e2;
    if (dx < 0) { dx = -dx; sx = -1; }
    if (dy < 0) { dy = -dy; sy = -1; }
    err = dx - dy;
    for (;;) {
        plot(x0, y0);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

int main(void)
{
    unsigned frame, i, angle = 0;

    __ftsetvp(vp);
    __vsetlen(8);

    for (frame = 0; frame < 6; frame++) {
        float sc[2], sc2[2], m[16], c, s, c2, s2;
        int px[8], py[8], andc;

        /* spin about Y, nod about X at half rate, camera 4 units back:
         * clip = P * T(0,0,-4) * Rx(angle/2) * Ry(angle), Wc = -Zview */
        __fsincos(sc, angle);
        __fsincos(sc2, angle >> 1);
        s = sc[0]; c = sc[1];
        s2 = sc2[0]; c2 = sc2[1];

        m[0]  = c;        m[1]  = 0.0f;  m[2]  = s;        m[3]  = 0.0f;
        m[4]  = s2 * s;   m[5]  = c2;    m[6]  = -s2 * c;  m[7]  = 0.0f;
        m[8]  = -c2 * s;  m[9]  = s2;    m[10] = c2 * c;   m[11] = -4.0f;
        m[12] = c2 * s;   m[13] = -s2;   m[14] = -c2 * c;  m[15] = 4.0f;

        andc = __ftranspb(recs, cube, m);

        for (i = 0; i < H; i++) {
            int j;
            for (j = 0; j < W; j++)
                fb[i][j] = (i == 0 || i == H - 1) ? '-' :
                           (j == 0 || j == W - 1) ? '|' : ' ';
            fb[i][W] = 0;
        }
        for (i = 0; i < 8; i++) {
            px[i] = (int)(recs[i * 8 + 0] + 0.5f);
            py[i] = (int)(recs[i * 8 + 1] + 0.5f);
        }
        for (i = 0; i < 12; i++)
            line(px[edges[i * 2]], py[edges[i * 2]],
                 px[edges[i * 2 + 1]], py[edges[i * 2 + 1]]);

        printf("frame %d andcode %d\n", frame, andc);
        for (i = 0; i < H; i++)
            printf("%s\n", fb[i]);

        angle += 0x0AAAu;        /* 24 degrees per frame */
    }
    return 0;
}
