/* PNG decoding for the dEngine port: zlib inflate (stored, fixed and
 * dynamic Huffman blocks), the five scanline filters, and conversion to the
 * RGBA8 layout dEngine's iOS loader produced through CoreGraphics:
 * rows top first, colour premultiplied by alpha when the image has alpha,
 * alpha 255 otherwise. Non-interlaced images of every PNG colour type and
 * bit depth are accepted; Adam7 interlacing is rejected. */
#include "include/stdlib.h"
#include "include/string.h"
#include "png.h"

/* ---------------------------------------------------------------- inflate */
#define FAST_BITS 9
#define FAST_MASK ((1 << FAST_BITS) - 1)
typedef struct {
    unsigned short fast[1 << FAST_BITS]; /* (length << 9) | symbol, 0 = slow */
    unsigned short firstcode[16], firstsymbol[16];
    int maxcode[17];
    unsigned char size[288];
    unsigned short value[288];
} huffman;

typedef struct {
    const unsigned char *in, *in_end;
    unsigned int bits;
    int count, overrun;
    unsigned char *out, *out_end, *out_start;
    huffman length, distance;
} inflater;

static int reverse_bits(int v, int n)
{
    int r = 0;
    while (n--) { r = (r << 1) | (v & 1); v >>= 1; }
    return r;
}

static int build(huffman *h, const unsigned char *lengths, int n)
{
    int i, k = 0, code = 0, next[16], sizes[17];
    memset(sizes, 0, sizeof(sizes));
    memset(h->fast, 0, sizeof(h->fast));
    for (i = 0; i < n; i++) sizes[lengths[i]]++;
    sizes[0] = 0;
    for (i = 1; i < 16; i++) if (sizes[i] > (1 << i)) return 0;
    for (i = 1; i < 16; i++) {
        next[i] = code;
        h->firstcode[i] = (unsigned short)code;
        h->firstsymbol[i] = (unsigned short)k;
        code += sizes[i];
        if (sizes[i] && code - 1 >= (1 << i)) return 0;
        h->maxcode[i] = code << (16 - i);
        code <<= 1;
        k += sizes[i];
    }
    h->maxcode[16] = 0x10000;
    for (i = 0; i < n; i++) {
        int s = lengths[i];
        if (s) {
            int c = next[s] - h->firstcode[s] + h->firstsymbol[s];
            h->size[c] = (unsigned char)s;
            h->value[c] = (unsigned short)i;
            if (s <= FAST_BITS) {
                int j = reverse_bits(next[s], s);
                while (j < (1 << FAST_BITS)) { h->fast[j] = (unsigned short)((s << 9) | i); j += 1 << s; }
            }
            next[s]++;
        }
    }
    return 1;
}

static void fill(inflater *z)
{
    while (z->count <= 24) {
        unsigned int byte = 0;
        if (z->in < z->in_end) byte = *z->in++; else z->overrun++;
        z->bits |= byte << z->count;
        z->count += 8;
    }
}

static unsigned int take(inflater *z, int n)
{
    unsigned int v;
    if (z->count < n) fill(z);
    v = z->bits & ((1u << n) - 1u);
    z->bits >>= n; z->count -= n;
    return v;
}

static int decode(inflater *z, const huffman *h)
{
    int b, s, k;
    if (z->count < 16) fill(z);
    b = h->fast[z->bits & FAST_MASK];
    if (b) { s = b >> 9; z->bits >>= s; z->count -= s; return b & 511; }
    k = reverse_bits((int)(z->bits & 0xffffu), 16);
    for (s = FAST_BITS + 1; k >= h->maxcode[s]; s++) ;
    if (s >= 16) return -1;
    b = (k >> (16 - s)) - h->firstcode[s] + h->firstsymbol[s];
    if (b >= 288 || h->size[b] != s) return -1;
    z->bits >>= s; z->count -= s;
    return h->value[b];
}

static const unsigned short length_base[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const unsigned char length_extra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const unsigned short distance_base[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const unsigned char distance_extra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int codes(inflater *z)
{
    unsigned char *out = z->out;
    for (;;) {
        int sym = decode(z, &z->length);
        if (sym < 256) {
            if (sym < 0 || out >= z->out_end) return 0;
            *out++ = (unsigned char)sym;
        } else if (sym == 256) {
            z->out = out;
            return 1;
        } else {
            int len, dist;
            unsigned char *from;
            sym -= 257;
            if (sym >= 29) return 0;
            len = length_base[sym] + (int)take(z, length_extra[sym]);
            sym = decode(z, &z->distance);
            if (sym < 0 || sym >= 30) return 0;
            dist = distance_base[sym] + (int)take(z, distance_extra[sym]);
            if (dist > out - z->out_start || len > z->out_end - out) return 0;
            from = out - dist;
            if (dist == 1) { unsigned char v = *from; while (len--) *out++ = v; }
            else while (len--) *out++ = *from++;
        }
    }
}

static int dynamic_tables(inflater *z)
{
    static const unsigned char order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    unsigned char lengths[286 + 32], clen[19];
    huffman code_lengths;
    int hlit = (int)take(z, 5) + 257, hdist = (int)take(z, 5) + 1, hclen = (int)take(z, 4) + 4, i, n = 0;
    memset(clen, 0, sizeof(clen));
    for (i = 0; i < hclen; i++) clen[order[i]] = (unsigned char)take(z, 3);
    if (!build(&code_lengths, clen, 19)) return 0;
    while (n < hlit + hdist) {
        int c = decode(z, &code_lengths), repeat;
        unsigned char fillv = 0;
        if (c < 0 || c >= 19) return 0;
        if (c < 16) { lengths[n++] = (unsigned char)c; continue; }
        if (c == 16) { if (!n) return 0; fillv = lengths[n - 1]; repeat = 3 + (int)take(z, 2); }
        else if (c == 17) repeat = 3 + (int)take(z, 3);
        else repeat = 11 + (int)take(z, 7);
        if (n + repeat > hlit + hdist) return 0;
        while (repeat--) lengths[n++] = fillv;
    }
    if (!lengths[256]) return 0;
    return build(&z->length, lengths, hlit) && build(&z->distance, lengths + hlit, hdist);
}

/* Inflates a zlib stream into out[0..out_size); returns bytes written or -1. */
static long zlib_inflate(const unsigned char *in, unsigned long in_size, unsigned char *out, unsigned long out_size)
{
    inflater *z;
    int last;
    long result;
    if (in_size < 2 || (in[0] & 15) != 8 || ((in[0] << 8) | in[1]) % 31 || (in[1] & 32)) return -1;
    z = malloc(sizeof(*z));
    if (!z) return -1;
    z->in = in + 2; z->in_end = in + in_size; z->bits = 0; z->count = 0; z->overrun = 0;
    z->out = z->out_start = out; z->out_end = out + out_size;
    do {
        int type;
        last = (int)take(z, 1);
        type = (int)take(z, 2);
        if (type == 0) {
            unsigned int len, nlen;
            take(z, z->count & 7);
            /* Whole bytes still in the bit buffer come first. */
            len = take(z, 16); nlen = take(z, 16);
            if ((len ^ 0xffffu) != nlen || len > (unsigned)(z->out_end - z->out)) { free(z); return -1; }
            while (len && z->count >= 8) { *z->out++ = (unsigned char)take(z, 8); len--; }
            if (len > (unsigned)(z->in_end - z->in)) { free(z); return -1; }
            memcpy(z->out, z->in, len); z->out += len; z->in += len;
        } else if (type == 1) {
            unsigned char lengths[288 + 32];
            int i;
            for (i = 0; i < 144; i++) lengths[i] = 8;
            for (; i < 256; i++) lengths[i] = 9;
            for (; i < 280; i++) lengths[i] = 7;
            for (; i < 288; i++) lengths[i] = 8;
            for (i = 0; i < 32; i++) lengths[288 + i] = 5;
            if (!build(&z->length, lengths, 288) || !build(&z->distance, lengths + 288, 32) || !codes(z)) { free(z); return -1; }
        } else if (type == 2) {
            if (!dynamic_tables(z) || !codes(z)) { free(z); return -1; }
        } else { free(z); return -1; }
        if (z->overrun > 4) { free(z); return -1; }
    } while (!last);
    result = (long)(z->out - out);
    free(z);
    return result;
}

/* -------------------------------------------------------------------- PNG */
static unsigned int be32(const unsigned char *p)
{
    return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | p[3];
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static unsigned char premultiply(unsigned int c, unsigned int a) { return (unsigned char)((c * a + 127u) / 255u); }

const char *de_png_decode(const unsigned char *data, unsigned long size, de_png_image *img)
{
    static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    unsigned long pos = 8, idat_size = 0, raw_size, stride, x, y;
    unsigned int width = 0, height = 0, depth = 0, type = 0, channels, bpp, i, palette_size = 0;
    unsigned char palette[256 * 4], *idat, *raw, *rgba, trns[6];
    int have_trns = 0, has_alpha;
    long produced;
    memset(img, 0, sizeof(*img));
    if (size < 8 || memcmp(data, signature, 8)) return "not a PNG file";
    for (i = 0; i < 256; i++) { palette[i * 4] = palette[i * 4 + 1] = palette[i * 4 + 2] = 0; palette[i * 4 + 3] = 255; }
    /* First pass: header, palette, transparency and the IDAT total. */
    while (pos + 12 <= size) {
        unsigned long len = be32(data + pos);
        const unsigned char *t = data + pos + 4, *d = data + pos + 8;
        if (len > size - pos - 12) return "truncated chunk";
        if (!memcmp(t, "IHDR", 4)) {
            if (len < 13) return "bad IHDR";
            width = be32(d); height = be32(d + 4); depth = d[8]; type = d[9];
            if (d[10] || d[11]) return "unknown compression or filter method";
            if (d[12]) return "interlaced PNG is not supported";
        } else if (!memcmp(t, "PLTE", 4)) {
            palette_size = (unsigned int)(len / 3);
            if (palette_size > 256) return "bad palette";
            for (i = 0; i < palette_size; i++) { palette[i * 4] = d[i * 3]; palette[i * 4 + 1] = d[i * 3 + 1]; palette[i * 4 + 2] = d[i * 3 + 2]; }
        } else if (!memcmp(t, "tRNS", 4)) {
            have_trns = 1;
            if (type == 3) for (i = 0; i < len && i < 256; i++) palette[i * 4 + 3] = d[i];
            else for (i = 0; i < len && i < 6; i++) trns[i] = d[i];
        } else if (!memcmp(t, "IDAT", 4)) idat_size += len;
        else if (!memcmp(t, "IEND", 4)) break;
        pos += len + 12;
    }
    if (!width || !height || width > 16384 || height > 16384) return "bad image size";
    channels = type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : type == 6 ? 4 : 0;
    if (!channels) return "bad colour type";
    if (!(depth == 8 || depth == 16 || ((type == 0 || type == 3) && (depth == 1 || depth == 2 || depth == 4)))) return "bad bit depth";
    if (type == 3 && depth == 16) return "bad bit depth";
    stride = ((unsigned long)width * channels * depth + 7u) / 8u;
    bpp = (channels * depth + 7u) / 8u;
    raw_size = (stride + 1u) * height;
    idat = malloc(idat_size ? idat_size : 1);
    raw = malloc(raw_size);
    if (!idat || !raw) { free(idat); free(raw); return "out of memory"; }
    /* Second pass: gather the IDAT payloads into one zlib stream. */
    idat_size = 0;
    for (pos = 8; pos + 12 <= size;) {
        unsigned long len = be32(data + pos);
        if (!memcmp(data + pos + 4, "IDAT", 4)) { memcpy(idat + idat_size, data + pos + 8, len); idat_size += len; }
        if (!memcmp(data + pos + 4, "IEND", 4)) break;
        pos += len + 12;
    }
    produced = zlib_inflate(idat, idat_size, raw, raw_size);
    free(idat);
    if (produced != (long)raw_size) { free(raw); return "corrupt image data"; }
    /* Undo the filters in place: each row follows its filter byte. */
    for (y = 0; y < height; y++) {
        unsigned char *row = raw + y * (stride + 1u) + 1u, *prior = y ? row - (stride + 1u) : 0;
        unsigned int filter = row[-1];
        if (filter > 4) { free(raw); return "bad filter type"; }
        for (x = 0; x < stride; x++) {
            int a = x >= bpp ? row[x - bpp] : 0, b = prior ? prior[x] : 0, c = (prior && x >= bpp) ? prior[x - bpp] : 0;
            switch (filter) {
            case 1: row[x] = (unsigned char)(row[x] + a); break;
            case 2: row[x] = (unsigned char)(row[x] + b); break;
            case 3: row[x] = (unsigned char)(row[x] + ((a + b) >> 1)); break;
            case 4: row[x] = (unsigned char)(row[x] + paeth(a, b, c)); break;
            default: break;
            }
        }
    }
    has_alpha = type == 4 || type == 6 || have_trns;
    rgba = malloc((unsigned long)width * height * 4u);
    if (!rgba) { free(raw); return "out of memory"; }
    for (y = 0; y < height; y++) {
        const unsigned char *row = raw + y * (stride + 1u) + 1u;
        unsigned char *o = rgba + y * width * 4u;
        for (x = 0; x < width; x++, o += 4) {
            unsigned int s[4], v, k;
            if (depth < 8) {
                unsigned long bit = x * depth;
                v = (row[bit >> 3] >> (8u - depth - (unsigned)(bit & 7u))) & ((1u << depth) - 1u);
                if (type == 3) { memcpy(o, palette + v * 4u, 4); continue; }
                s[0] = s[1] = s[2] = v * 255u / ((1u << depth) - 1u);
                s[3] = have_trns && v == (unsigned)((trns[0] << 8) | trns[1]) ? 0 : 255;
            } else {
                for (k = 0; k < channels; k++)
                    s[k] = depth == 8 ? row[x * channels + k] : row[(x * channels + k) * 2u];
                if (type == 3) { memcpy(o, palette + s[0] * 4u, 4); continue; }
                if (type == 0) { v = s[0]; s[3] = 255; s[0] = s[1] = s[2] = v;
                    if (have_trns && (depth == 8 ? v == trns[1] : v == trns[0])) s[3] = 0; }
                else if (type == 4) { s[3] = s[1]; s[1] = s[2] = s[0]; }
                else if (type == 2) {
                    s[3] = 255;
                    if (have_trns && (depth == 8 ? (s[0] == trns[1] && s[1] == trns[3] && s[2] == trns[5])
                                                 : (s[0] == trns[0] && s[1] == trns[2] && s[2] == trns[4]))) s[3] = 0;
                }
            }
            o[0] = (unsigned char)s[0]; o[1] = (unsigned char)s[1]; o[2] = (unsigned char)s[2]; o[3] = (unsigned char)s[3];
        }
    }
    free(raw);
    if (has_alpha) {
        unsigned long n = (unsigned long)width * height;
        unsigned char *p = rgba;
        for (; n--; p += 4) if (p[3] != 255) { p[0] = premultiply(p[0], p[3]); p[1] = premultiply(p[1], p[3]); p[2] = premultiply(p[2], p[3]); }
    }
    img->width = width; img->height = height; img->rgba = rgba;
    img->has_alpha = has_alpha;
    img->bits_per_pixel = has_alpha ? 32 : type == 2 ? 24 : 8 * channels;
    return 0;
}
