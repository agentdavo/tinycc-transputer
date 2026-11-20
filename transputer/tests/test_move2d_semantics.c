/* Expected: SMOVE2DOK\nH */
/* Needs: t800ops */
static unsigned char src[32], dst[40], expected[40];
static volatile int evaluations;

static int run(int mode, int width, int rows)
{
    int i, x, y;
    for (i = 0; i < 32; i++)
        src[i] = (i % 3) ? (unsigned char)(i + 1) : 0;
    for (i = 0; i < 40; i++)
        dst[i] = expected[i] = 0x5a;
    for (y = 0; y < rows; y++)
        for (x = 0; x < width; x++) {
            unsigned char value = src[y * 8 + x];
            if (mode == 0 || (mode == 1 && value) || (mode == 2 && !value))
                expected[y * 10 + x] = value;
        }

    evaluations = 0;
    if (mode == 0) {
        __move2dinit((evaluations++, rows), 10, 8);
        __move2dall((evaluations++, width), dst, src);
    } else if (mode == 1) {
        __move2dinit((evaluations++, rows), 10, 8);
        __move2dnonzero((evaluations++, width), dst, src);
    } else {
        __move2dinit((evaluations++, rows), 10, 8);
        __move2dzero((evaluations++, width), dst, src);
    }
    if (evaluations != 2)
        return 0;
    for (i = 0; i < 40; i++)
        if (dst[i] != expected[i])
            return 0;
    return 1;
}

void test_main(void)
{
    int mode, ok = 1;
    const char *message;
    for (mode = 0; mode < 3; mode++) {
        ok &= run(mode, 5, 3);
        ok &= run(mode, 2, 2);
        ok &= run(mode, 0, 3);
        ok &= run(mode, 5, 0);
    }
    message = ok ? "MOVE2DOK\n" : "FAIL\n";
    while (*message)
        *(volatile unsigned *)0x10000000u = (unsigned char)*message++;
}
