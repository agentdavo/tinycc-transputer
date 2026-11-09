/* PVRTC1 (2 and 4 bits per pixel) decompression to RGBA8, for
 * glCompressedTexImage2D with the GL_IMG_texture_compression_pvrtc formats.
 *
 * Follows the PowerVR reference decompressor: 64-bit blocks in Morton
 * order, each holding two low-resolution colours (A and B, expanded to
 * 5554) and per-pixel modulation weights; A and B are upscaled bilinearly
 * between the centres of neighbouring blocks (wrapping at the edges) and
 * mixed by the weight. The reference transposes its 4bpp intermediate
 * arrays twice; this version keeps x and y in place, which gives the same
 * pixels. Rows are written in data order, row 0 first. */
#include "include/stdlib.h"
#include "include/string.h"
#include "pvrtc.h"

typedef struct { int r, g, b, a; } pixel;

static unsigned int le32(const unsigned char *p)
{
    return (unsigned int)p[0] | (unsigned int)p[1] << 8 | (unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}

static pixel colour_a(unsigned int c)
{
    pixel p;
    if (c & 0x8000u) {                       /* opaque RGB 554 */
        p.r = (int)((c & 0x7c00u) >> 10);
        p.g = (int)((c & 0x3e0u) >> 5);
        p.b = (int)((c & 0x1eu) | ((c & 0x1eu) >> 4));
        p.a = 0xf;
    } else {                                 /* translucent ARGB 3443 */
        p.r = (int)(((c & 0xf00u) >> 7) | ((c & 0xf00u) >> 11));
        p.g = (int)(((c & 0xf0u) >> 3) | ((c & 0xf0u) >> 7));
        p.b = (int)(((c & 0xeu) << 1) | ((c & 0xeu) >> 2));
        p.a = (int)((c & 0x7000u) >> 11);
    }
    return p;
}

static pixel colour_b(unsigned int c)
{
    pixel p;
    if (c & 0x80000000u) {                   /* opaque RGB 555 */
        p.r = (int)((c & 0x7c000000u) >> 26);
        p.g = (int)((c & 0x3e00000u) >> 21);
        p.b = (int)((c & 0x1f0000u) >> 16);
        p.a = 0xf;
    } else {                                 /* translucent ARGB 3444 */
        p.r = (int)(((c & 0xf000000u) >> 23) | ((c & 0xf000000u) >> 27));
        p.g = (int)(((c & 0xf00000u) >> 19) | ((c & 0xf00000u) >> 23));
        p.b = (int)(((c & 0xf0000u) >> 15) | ((c & 0xf0000u) >> 19));
        p.a = (int)((c & 0x70000000u) >> 27);
    }
    return p;
}

static unsigned int twiddle(unsigned int xwords, unsigned int ywords, unsigned int x, unsigned int y)
{
    /* Morton order over the smaller dimension, the rest of the larger
     * coordinate above it: y bits even, x bits odd. Checked against the
     * PNG sources of the dEngine PVR files (test_pvrtc.c). */
    unsigned int min = ywords, rest = x, out = 0, src = 1, dst = 1, shift = 0;
    if (xwords < ywords) { min = xwords; rest = y; }
    while (src < min) {
        if (y & src) out |= dst;
        if (x & src) out |= dst << 1;
        src <<= 1; dst <<= 2; shift++;
    }
    return out | ((rest >> shift) << (2 * shift));
}

typedef struct { unsigned int modulation, colour; } word;

/* Modulation grid for the 2x2 words around a decoded region: 16x8 values
 * (2bpp) or 8x8 (4bpp), indexed [x][y]. */
typedef struct { int value[16][8], mode[16][8]; } modulation_grid;

static void unpack(const word *w, int ox, int oy, modulation_grid *g, int two_bpp)
{
    unsigned int bits = w->modulation, mode = w->colour & 1u;
    int x, y;
    if (two_bpp) {
        if (mode) {
            if (bits & 1u) {
                mode = (bits & (1u << 20)) ? 3 : 2;  /* V-only : H-only */
                if (bits & (1u << 21)) bits |= 1u << 20; else bits &= ~(1u << 20);
            }
            if (bits & 2u) bits |= 1u; else bits &= ~1u;
            for (y = 0; y < 4; y++)
                for (x = 0; x < 8; x++) {
                    g->mode[ox + x][oy + y] = (int)mode;
                    if (((x ^ y) & 1) == 0) { g->value[ox + x][oy + y] = (int)(bits & 3u); bits >>= 2; }
                }
        } else {
            for (y = 0; y < 4; y++)
                for (x = 0; x < 8; x++) {
                    g->mode[ox + x][oy + y] = 0;
                    g->value[ox + x][oy + y] = (bits & 1u) ? 3 : 0;
                    bits >>= 1;
                }
        }
    } else {
        for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++) {
                int v = (int)(bits & 3u);
                if (mode) v = v == 1 ? 4 : v == 2 ? 14 : v == 3 ? 8 : 0; /* 14: 4 + punch-through */
                else { v *= 3; if (v > 3) v--; }
                g->value[ox + x][oy + y] = v;
                bits >>= 2;
            }
    }
}

static int weight(const modulation_grid *g, int x, int y, int two_bpp)
{
    static const int rep[4] = {0, 3, 5, 8};
    if (!two_bpp) return g->value[x][y];
    if (g->mode[x][y] == 0 || ((x ^ y) & 1) == 0) return rep[g->value[x][y]];
    if (g->mode[x][y] == 1)
        return (rep[g->value[x][y - 1]] + rep[g->value[x][y + 1]] + rep[g->value[x - 1][y]] + rep[g->value[x + 1][y]] + 2) / 4;
    if (g->mode[x][y] == 2) return (rep[g->value[x - 1][y]] + rep[g->value[x + 1][y]] + 1) / 2;
    return (rep[g->value[x][y - 1]] + rep[g->value[x][y + 1]] + 1) / 2;
}

/* Bilinear upscale of four block colours to the W x H region whose top-left
 * pixel sits at P's centre: x runs towards Q, y towards R. */
static void upscale(pixel P, pixel Q, pixel R, pixel S, int w, int h, pixel *out)
{
    int x, y, scale = w * h;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int top_r = P.r * (w - x) + Q.r * x, top_g = P.g * (w - x) + Q.g * x, top_b = P.b * (w - x) + Q.b * x, top_a = P.a * (w - x) + Q.a * x;
            int bot_r = R.r * (w - x) + S.r * x, bot_g = R.g * (w - x) + S.g * x, bot_b = R.b * (w - x) + S.b * x, bot_a = R.a * (w - x) + S.a * x;
            int r = top_r * (h - y) + bot_r * y, g = top_g * (h - y) + bot_g * y, b = top_b * (h - y) + bot_b * y, a = top_a * (h - y) + bot_a * y;
            pixel *o = &out[y * w + x];
            /* Scale is 32 (2bpp) or 16 (4bpp): 5-bit colour to 8 bits is
             * c*8.25, 4-bit alpha c*17, the reference's shift pairs. */
            if (scale == 32) {
                o->r = (r >> 7) + (r >> 2); o->g = (g >> 7) + (g >> 2); o->b = (b >> 7) + (b >> 2); o->a = (a >> 5) + (a >> 1);
            } else {
                o->r = (r >> 6) + (r >> 1); o->g = (g >> 6) + (g >> 1); o->b = (b >> 6) + (b >> 1); o->a = (a >> 4) + a;
            }
        }
}

int de_pvrtc_decode(const unsigned char *data, unsigned int width, unsigned int height, int two_bpp, int force_opaque, unsigned char *rgba)
{
    int W = two_bpp ? 8 : 4, H = 4, wx, wy, x, y;
    unsigned int xdim = width, ydim = height, xwords, ywords;
    unsigned char *out = rgba;
    modulation_grid *grid;
    pixel *ua, *ub;
    if (!width || !height || (width & (width - 1)) || (height & (height - 1))) return 0;
    if (xdim < (unsigned)(2 * W)) xdim = (unsigned)(2 * W);
    if (ydim < (unsigned)(2 * H)) ydim = (unsigned)(2 * H);
    xwords = xdim / (unsigned)W; ywords = ydim / (unsigned)H;
    grid = malloc(sizeof(*grid));
    ua = malloc(sizeof(pixel) * 32u); ub = malloc(sizeof(pixel) * 32u);
    if (xdim != width || ydim != height) out = malloc((size_t)xdim * ydim * 4u);
    if (!grid || !ua || !ub || !out) { free(grid); free(ua); free(ub); if (out != rgba) free(out); return 0; }
    for (wy = -1; wy < (int)ywords - 1; wy++)
        for (wx = -1; wx < (int)xwords - 1; wx++) {
            unsigned int px = (unsigned)(wx + (int)xwords) % xwords, py = (unsigned)(wy + (int)ywords) % ywords;
            unsigned int qx = (unsigned)(wx + 1) % xwords, ry = (unsigned)(wy + 1) % ywords;
            unsigned int index[4];
            word w[4];
            int k;
            index[0] = twiddle(xwords, ywords, px, py);
            index[1] = twiddle(xwords, ywords, qx, py);
            index[2] = twiddle(xwords, ywords, px, ry);
            index[3] = twiddle(xwords, ywords, qx, ry);
            for (k = 0; k < 4; k++) {
                w[k].modulation = le32(data + index[k] * 8u);
                w[k].colour = le32(data + index[k] * 8u + 4u);
            }
            unpack(&w[0], 0, 0, grid, two_bpp);
            unpack(&w[1], W, 0, grid, two_bpp);
            unpack(&w[2], 0, H, grid, two_bpp);
            unpack(&w[3], W, H, grid, two_bpp);
            upscale(colour_a(w[0].colour), colour_a(w[1].colour), colour_a(w[2].colour), colour_a(w[3].colour), W, H, ua);
            upscale(colour_b(w[0].colour), colour_b(w[1].colour), colour_b(w[2].colour), colour_b(w[3].colour), W, H, ub);
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++) {
                    int mod = weight(grid, x + W / 2, y + H / 2, two_bpp), punch = 0;
                    const pixel *A = &ua[y * W + x], *B = &ub[y * W + x];
                    unsigned int ox = ((unsigned)(wx * W + W / 2 + x) + xdim) % xdim;
                    unsigned int oy = ((unsigned)(wy * H + H / 2 + y) + ydim) % ydim;
                    unsigned char *o = out + ((size_t)oy * xdim + ox) * 4u;
                    if (mod > 10) { punch = 1; mod -= 10; }
                    o[0] = (unsigned char)((A->r * (8 - mod) + B->r * mod) / 8);
                    o[1] = (unsigned char)((A->g * (8 - mod) + B->g * mod) / 8);
                    o[2] = (unsigned char)((A->b * (8 - mod) + B->b * mod) / 8);
                    o[3] = (unsigned char)(punch ? 0 : (A->a * (8 - mod) + B->a * mod) / 8);
                    if (force_opaque) o[3] = 255;
                }
        }
    if (out != rgba) {
        for (y = 0; y < (int)height; y++) memcpy(rgba + (size_t)y * width * 4u, out + (size_t)y * xdim * 4u, width * 4u);
        free(out);
    }
    free(grid); free(ua); free(ub);
    return 1;
}

unsigned int de_pvrtc_size(unsigned int width, unsigned int height, int two_bpp)
{
    unsigned int w = width, h = height;
    if (two_bpp) { if (w < 16) w = 16; } else if (w < 8) w = 8;
    if (h < 8) h = 8;
    return w * h * (two_bpp ? 2u : 4u) / 8u;
}
