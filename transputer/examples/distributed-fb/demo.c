/* Four independent T800s render local tiles; node 0 assembles the display. */
#include <conc.h>
#include "tinygl.h"

#define TW 80
#define TH 60
#define SCALE 2
#define WIDTH 320
#define HEIGHT 240
#define FB_BASE 0x90000000u
#define FRAME_BYTES (WIDTH * HEIGHT * 4u)
#define REG(a) (*(volatile unsigned int *)(a))
#define UART REG(0x10000000u)
#ifndef FRAMES
#define FRAMES 12
#endif
#ifndef FPS
#define FPS 20
#endif
#ifndef CAPTURE
#define CAPTURE 1
#endif

void __in(int, Channel *, void *);
void __out(int, Channel *, const void *);

static unsigned int tile[TW * TH];
static int depth[TW * TH];
static unsigned int header[2];
#if NODE == 0
static unsigned int expanded[2 * TW];
void __move2dinit(int width, int dest_stride, int src_stride);
void __move2dall(int length, void *dest, const void *src);
#endif

static void render(int frame)
{
    cube_render(tile, depth, NODE, frame);
}

#if NODE == 0
static void puts_uart(const char *s) { while (*s) UART = (unsigned char)*s++; }
static void hex_uart(unsigned int v)
{
    int shift;
    const char *digits = "0123456789abcdef";
    for (shift = 28; shift >= 0; shift -= 4) UART = digits[(v >> shift) & 15];
}

static void display_tile(int owner, unsigned int draw_base)
{
    int x, y;
    int ox = (owner & 1) * TW * SCALE;
    int oy = (owner >> 1) * TH * SCALE;
    for (y = 0; y < TH; ++y) {
        for (x = 0; x < TW; ++x) {
            unsigned int p = tile[y * TW + x];
            expanded[x*2] = p;
            expanded[x*2+1] = p;
        }
        __move2dinit(2, WIDTH * 4, 0);
        __move2dall(sizeof(expanded), (void *)(draw_base + ((oy+y*2)*WIDTH+ox)*4), expanded);
    }
}
#endif

int test_main(void)
{
    unsigned int frame;
#if NODE == 0
    int owner, bad = 0;
    unsigned int next_flip = 0, presented;
    REG(0xA0000004u) = FB_BASE;
    REG(0xA0000008u) = WIDTH;
    REG(0xA000000cu) = HEIGHT;
    REG(0xA0000010u) = WIDTH * 4;
    REG(0xA0000000u) = 1;
    for (frame = 0; frame < FRAMES; ++frame) {
        /* Buffer 0 is initially visible, so the first frame uses buffer 1. */
        unsigned int draw_base = FB_BASE + ((frame & 1u) ? 0u : FRAME_BYTES);
        /* Commands are the frame barrier: no worker can send the next tile
         * until the previous frame has been assembled and captured. */
        for (owner = 1; owner <= 3; ++owner)
            __out(sizeof(frame), LINKOUT(owner), &frame);
        render(frame);
        display_tile(0, draw_base);
        for (owner = 1; owner <= 3; ++owner) {
            __in(sizeof(header), LINKIN(owner), header);
            __in(sizeof(tile), LINKIN(owner), tile);
            if (header[0] != frame || header[1] != tg_checksum(tile, TW*TH)) ++bad;
            display_tile(owner, draw_base);
        }
        /* The bootstrap runs at low priority, whose timer ticks every 64 us.
         * Schedule from the last actual flip: late work cannot cause bursts. */
        if (frame && TimeAfter(next_flip, (unsigned int)Time()))
            ProcAfter((int)next_flip);
        REG(0xA0000014u) = draw_base;
        presented = (unsigned int)Time();
        next_flip = presented + (TICKS_PER_SEC_LOW + FPS - 1u) / FPS;
        puts_uart("DISTFB FRAME "); hex_uart(frame); UART = ' ';
        hex_uart(draw_base); UART = ' '; hex_uart(presented); UART = '\n';
#if CAPTURE
        REG(0xA0000018u) = 1; /* capture the displayed buffer */
#endif
    }
    puts_uart(bad ? "DISTFB BAD\n" : "DISTFB OK\n");
    __in(sizeof(frame), LINKIN(1), &frame);
#else
    for (;;) {
        __in(sizeof(frame), LINKIN(1), &frame);
        render(frame);
        header[0] = frame;
        header[1] = tg_checksum(tile, TW*TH);
        __out(sizeof(header), LINKOUT(1), header);
        __out(sizeof(tile), LINKOUT(1), tile);
    }
#endif
    return 0;
}
