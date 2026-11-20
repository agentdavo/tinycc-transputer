/*
 * test_loop_end.c - `do ... while (c--)` loops whose counter only counts end
 * on the pointer they step instead (tp_loop_end): Doom's column and span
 * drawers.  Checks byte and word steps up and down, a stride, a count of 0
 * (one trip), a long run, a loop whose counter is read again afterwards
 * (must keep it), one with a call in the body, and pointers into globals
 * and locals.  One letter per check: upper case passes, '!' fails.
 *
 * K: the counter reloaded in the body (`count = *next++`) - the end must
 * not live in its word; L: a volatile counter and a volatile pointer.
 *
 * Expected: SABCDEFGHIJKL\nH
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static void check(int ok, int letter)
{
    uart_putc(ok ? letter : '!');
}

/* one spare row: a column's last step leaves dest a stride past its last
 * pixel, which must still point into (or one past) the array */
static unsigned char screen[64 * 41];
static int words[300];

/* column: dest += 64, count + 1 rows (Doom's R_DrawColumn shape) */
static void column(unsigned char *dest, int count, const unsigned char *map,
                   const unsigned char *src)
{
    int frac = 0;
    do {
        *dest = map[src[(frac >> 4) & 15]];
        dest += 64;
        frac += 3;
    } while (count--);
}

/* span: *dest++ (R_DrawSpan shape) */
static void span(unsigned char *dest, int count, unsigned char v)
{
    do {
        *dest++ = v;
        v += 1;
    } while (count--);
}

static void down(int *p, int count)
{
    int k = 0;
    do {
        *p = k++;
        p--;
    } while (count--);
}

static int keep_counter(unsigned char *d, int count)
{
    do {
        *d++ = 7;
    } while (count--);
    return count;           /* read after: the counter must stay */
}

static int calls;
static int bump(int x)
{
    calls++;
    return x + 1;
}

static void with_call(int *p, int count)
{
    do {
        *p = bump(*p);
        p++;
    } while (count--);
}

/* the counter reloaded from a list inside the body: 3 trips, then the
 * zero read in the last trip ends it */
static int reload(unsigned char *dest, const int *next)
{
    unsigned char *d0 = dest;
    int count = 2;
    do {
        *dest++ = 9;
        if (count == 1)
            count = *next++;
    } while (count--);
    return (int)(dest - d0);
}

static int reload2(unsigned char *dest, const int *next)
{
    unsigned char *d0 = dest;
    int count = 0;
    do {
        *dest++ = 8;
        count = *next++;
    } while (count--);
    return (int)(dest - d0);
}

static void vol(unsigned char *dest, int count)
{
    volatile int c = count;
    unsigned char *volatile p = dest;
    do {
        *p = 6;
        p++;
    } while (c--);
}

void test_main(void)
{
    static unsigned char map[256], src[16];
    unsigned char local[32];
    int i, ok;

    for (i = 0; i < 256; i++)
        map[i] = (unsigned char)(255 - i);
    for (i = 0; i < 16; i++)
        src[i] = (unsigned char)(i * 3);
    for (i = 0; i < (int)sizeof screen; i++)
        screen[i] = 0xAA;

    column(screen + 5, 9, map, src);       /* rows 0..9 at x = 5 */
    for (ok = 1, i = 0; i < 40; i++) {
        int frac = i * 3;
        unsigned char want = i <= 9 ? map[src[(frac >> 4) & 15]] : 0xAA;
        if (screen[i * 64 + 5] != want || screen[i * 64 + 4] != 0xAA ||
            screen[i * 64 + 6] != 0xAA)
            ok = 0;
    }
    check(ok, 'A');

    column(screen + 9, 0, map, src);        /* count 0: one row */
    check(screen[9] == map[src[0]] && screen[64 + 9] == 0xAA, 'B');

    span(screen + 64 * 20 + 1, 30, 40);
    for (ok = 1, i = 0; i < 33; i++) {
        unsigned char got = screen[64 * 20 + i];
        unsigned char want = i == 0 || i == 32 ? 0xAA : (unsigned char)(39 + i);
        if (got != want)
            ok = 0;
    }
    check(ok, 'C');

    span(local, 0, 9);
    check(local[0] == 9, 'D');

    for (i = 0; i < 300; i++)
        words[i] = -5;
    down(words + 250, 199);                 /* 250 down to 51 */
    for (ok = 1, i = 0; i < 300; i++) {
        int want = i >= 51 && i <= 250 ? 250 - i : -5;
        if (words[i] != want)
            ok = 0;
    }
    check(ok, 'E');

    check(keep_counter(local, 5) == -1 && local[5] == 7, 'F');

    for (i = 0; i < 10; i++)
        words[i] = i;
    calls = 0;
    with_call(words, 9);
    for (ok = 1, i = 0; i < 10; i++)
        if (words[i] != i + 1)
            ok = 0;
    check(ok && calls == 10 && words[10] == -5, 'G');

    /* a long run through a local array, then the bytes either side */
    {
        unsigned char big[600];
        for (i = 0; i < 600; i++)
            big[i] = 1;
        span(big + 50, 499, 0);
        ok = big[49] == 1 && big[550] == 1 && big[50] == 0 &&
             big[549] == (unsigned char)499;
        check(ok, 'H');
    }

    /* drawers back to back, sharing nothing */
    span(local, 15, 100);
    span(local + 16, 15, 200);
    check(local[15] == 115 && local[16] == 200 && local[31] == 215, 'I');

    column(screen + 63, 39, map, src);      /* the whole last column */
    check(screen[39 * 64 + 63] == map[src[((39 * 3) >> 4) & 15]], 'J');

    {
        static const int list[] = { 3, 1, 0, 0 };
        unsigned char big[40];
        int a, b;
        for (i = 0; i < 40; i++)
            big[i] = 0;
        /* trips: 2->1; reload 3 ->2; 2->1; reload 1 ->0; 0 ends: 5 */
        a = reload(big, list);
        /* each trip reloads: 3 ->2, 1 ->0, 0 ends: 3 */
        b = reload2(big + 20, list);
        check(a == 5 && big[4] == 9 && big[5] == 0 &&
              b == 3 && big[22] == 8 && big[23] == 0, 'K');
    }

    for (i = 0; i < 32; i++)
        local[i] = 0;
    vol(local + 2, 4);
    check(local[1] == 0 && local[2] == 6 && local[6] == 6 && local[7] == 0,
          'L');
    uart_putc('\n');
}
