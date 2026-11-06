/* Rectangular surface copies: row-by-row MOVE versus MOVE2DALL.
 * The source and destination have different strides and guard pixels. */
typedef unsigned int u32;
void *memcpy(void *, const void *, unsigned long);
unsigned int __ldtimer(void);
#define UART (*(volatile unsigned int *)0x10000000u)
static void put(const char *s) { while (*s) UART = (unsigned char)*s++; }
static void num(u32 n) { char b[12]; int i = 0; do { b[i++] = '0' + n % 10; } while (n /= 10); while (i) UART = b[--i]; }
static u32 hash(const u32 *p, int n) { u32 h = 2166136261u; while (n--) h = (h ^ *p++) * 16777619u; return h; }

#define W 320
#define H 240
#define SS 337
#define DS 800
#define REPEAT 12
static u32 src[SS * H + 32], rows[DS * H + 32], rect[DS * H + 32];

static void rowcopy(u32 *dst, const u32 *s, int width, int height)
{
    int row, bytes = width * 4;
    for (row = 0; row < height; row++) {
        u32 *d = dst + row * DS;
        const u32 *p = s + row * SS;
        memcpy(d, p, bytes);
    }
}

static void rectcopy(u32 *dst, const u32 *s, int width, int height)
{
    int bytes = width * 4, dstride = DS * 4, sstride = SS * 4;
    __move2dinit(height, dstride, sstride);
    __move2dall(bytes, dst, s);
}

void test_main(void)
{
    int i, n, bad = 0;
    u32 t, rowtime, recttime, sh;
    for (i = 0; i < (int)(sizeof src / sizeof *src); i++)
        src[i] = 0x9e3779b9u * i + (i >> 3);
    for (i = 0; i < (int)(sizeof rows / sizeof *rows); i++)
        rows[i] = rect[i] = 0xface0000u ^ (u32)i;
    sh = hash(src, sizeof src / sizeof *src);
    t = __ldtimer();
    for (n = 0; n < REPEAT; n++) rowcopy(rows + 7, src + 11, W, H);
    rowtime = __ldtimer() - t;
    t = __ldtimer();
    for (n = 0; n < REPEAT; n++) rectcopy(rect + 7, src + 11, W, H);
    recttime = __ldtimer() - t;
    for (i = 0; i < (int)(sizeof rows / sizeof *rows); i++)
        if (rows[i] != rect[i]) bad++;
    if (sh != hash(src, sizeof src / sizeof *src)) bad++;
    /* Both zero dimensions are specified to leave the destination alone. */
    rectcopy(rect + 7, src + 11, 0, H);
    rectcopy(rect + 7, src + 11, W, 0);
    for (i = 0; i < (int)(sizeof rows / sizeof *rows); i++)
        if (rows[i] != rect[i]) bad++;
    /* The current t4 estimator does not charge MOVE2DALL for transferred
     * data. Its timer value is useful for overhead only, not total cycles. */
    put("rowcopy "); num(rowtime); put("\nmove2d_overhead "); num(recttime);
    put("\nhash "); num(hash(rect, sizeof rect / sizeof *rect));
    put("\nfail "); num(bad); put("\n");
}
