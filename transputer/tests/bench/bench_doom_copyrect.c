/* Profile the real V_CopyRect with Freedoom status-bar patch sizes. */
#include "doomdef.h"
#include "m_bbox.h"
#include "v_video.h"
#include <stdio.h>

#define UART (*(volatile unsigned int *)0x10000000u)
#define GUARD 4
#define SIZE (SCREENWIDTH * SCREENHEIGHT + 2 * GUARD)
#define REPEATS 256

static byte source[SIZE], target[SIZE], expected[SIZE];
static int failures;
FILE *stderr;

static void put(const char *s)
{
    while (*s)
        UART = (unsigned char)*s++;
}

static void number(unsigned n)
{
    char digits[10];
    int used = 0;
    do {
        digits[used++] = '0' + n % 10;
    } while (n /= 10);
    while (used)
        UART = digits[--used];
}

unsigned int __ldtimer(void);

void I_Error(char *message, ...)
{
    put(message);
    __seterr();
    __stoperr();
    for (;;) {}
}

byte *I_AllocLow(int length)
{
    (void)length;
    I_Error("unexpected allocation");
    return 0;
}

int fprintf(FILE *stream, const char *format, ...)
{
    (void)stream;
    (void)format;
    I_Error("unexpected patch diagnostic");
    return 0;
}

static byte pattern(unsigned i)
{
    return i * 37u + (i >> 8) + 3u;
}

static void reset(void)
{
    int i;
    for (i = 0; i < SIZE; i++) {
        source[i] = pattern(i);
        target[i] = expected[i] = pattern(i + 113);
    }
    M_ClearBox(dirtybox);
}

static void reference(int sx, int sy, int dx, int dy, int w, int h, int shared)
{
    const byte *src = (shared ? expected : source) + GUARD + sy * SCREENWIDTH + sx;
    byte *dst = expected + GUARD + dy * SCREENWIDTH + dx;
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            dst[y * SCREENWIDTH + x] = src[y * SCREENWIDTH + x];
}

static unsigned check(int dx, int dy, int w, int h)
{
    unsigned hash = 2166136261u;
    int i;
    for (i = 0; i < SIZE; i++) {
        if (target[i] != expected[i] || source[i] != pattern(i))
            failures = 1;
        hash = (hash ^ target[i]) * 16777619u;
    }
    if (w && h && (dirtybox[BOXLEFT] != dx || dirtybox[BOXRIGHT] != dx + w - 1 ||
                   dirtybox[BOXBOTTOM] != dy || dirtybox[BOXTOP] != dy + h - 1))
        failures = 1;
    return hash;
}

static void trial(int sx, int sy, int dx, int dy, int w, int h, int shared)
{
    reset();
    screens[4] = (shared ? target : source) + GUARD;
    V_CopyRect(sx, sy, 4, w, h, dx, dy, 0);
    reference(sx, sy, dx, dy, w, h, shared);
    check(dx, dy, w, h);
}

static void measure(const char *name, int x, int y, int w, int h)
{
    int i, dy = SCREENHEIGHT - 32 + y;
    unsigned start, elapsed, hash;
    reset();
    screens[4] = source + GUARD;
    start = __ldtimer();
    for (i = 0; i < REPEATS; i++)
        V_CopyRect(x, y, 4, w, h, x, dy, 0);
    elapsed = __ldtimer() - start;
    reference(x, y, x, dy, w, h, 0);
    hash = check(x, dy, w, h);
    put(name);
    put(" ");
    number(elapsed);
    put(" ");
    number(hash);
    put("\n");
}

void test_main(void)
{
    int s, d;
    screens[0] = target + GUARD;
    for (s = 0; s < 4; s++)
        for (d = 0; d < 4; d++)
            trial(8 + s, 2, 40 + d, 23, 13, 16, 0);
    trial(9, 2, 9, 3, 13, 16, 1);  /* each row supplies the next one */
    trial(9, 2, 22, 9, 13, 8, 1);  /* spans exactly touch */
    trial(9, 2, 22, 2, 13, 8, 1);  /* interleaved disjoint rows */
    trial(8, 2, 40, 23, 0, 16, 0);
    trial(8, 2, 40, 23, 13, 0, 0);
    trial(8, 2, 40, 23, 13, 1, 0);

    measure("number", 5, 3, 39, 16); /* three STTNUM digits */
    measure("shortnum", 276, 5, 12, 6); /* three STYSNUM digits */
    measure("key", 239, 3, 7, 5); /* STKEYS */
    measure("face", 148, 2, 24, 29); /* STFST */
    measure("eight", 276, 5, 12, 8);
    measure("full", 0, 0, SCREENWIDTH, 32);
    put("check ");
    number(failures);
    put("\n");
}
