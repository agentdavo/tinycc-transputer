/* T4 node driver for the dEngine port.
 *
 * Every node boots the same program: it mounts the ROM image linked into it,
 * starts dEngine exactly as the iOS application did, and then draws frames.
 * The 320x480 window is split into BAND_ROWS-row bands; node n draws the
 * bands t with t % NNODES == n (interleaved, which spreads the scene's cost
 * evenly). Nodes form a binary tree over their links, as in
 * distributed-fb/scale.c: each sends its bands, with frame number, band
 * number, checksum and its render time, to its parent, which forwards its
 * children's bands. Node 0 checks that every band arrived once, intact and
 * for the right frame, and writes it to the display. The display is the
 * iPhone held in landscape: the portrait buffer is rotated to 480x320.
 *
 * Only node 0 prints; other nodes' console output is dropped.
 *
 * RAM per node (64 MiB, t4 -sm 26): image at MemStart, heap up to the t4
 * framebuffer window at 0x81E00000, heap again from 0x82000000 to
 * 0x83E00000, workspace growing down from 63 MiB (boot.S). */
#include <conc.h>
#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/string.h"
#include "platform.h"
#include "gles2_port.h"
#include "dengine_app.h"

#ifndef NODE
#define NODE 0
#endif
#ifndef NNODES
#define NNODES 1
#endif
#ifndef FRAMES
#define FRAMES 2
#endif
#ifndef CAPTURE
#define CAPTURE 1
#endif
#ifndef WIDTH
#define WIDTH 320
#endif
#ifndef HEIGHT
#define HEIGHT 480
#endif
#ifndef BAND_ROWS
#define BAND_ROWS 5
#endif
#ifndef START_MS
#define START_MS 0
#endif
#ifndef FRAME_MS
#define FRAME_MS 33
#endif
#ifndef LOW_QUALITY
#define LOW_QUALITY 0
#endif
#ifndef SHADOWS
#define SHADOWS 1
#endif
#ifndef BUMP
#define BUMP 1
#endif
#ifndef SPECULAR
#define SPECULAR 1
#endif
#ifndef STATS
#define STATS 0
#endif
#ifndef CAMERA_PATH
#define CAMERA_PATH 1
#endif
#ifndef ROTATE_DISPLAY
#define ROTATE_DISPLAY 1
#endif

#define BANDS (HEIGHT / BAND_ROWS)
#define BAND_WORDS (BAND_ROWS * WIDTH)
#define UART (*(volatile unsigned int *)0x10000000u)
#define REG(a) (*(volatile unsigned int *)(a))
#define FB_BASE 0x90000000u
#if ROTATE_DISPLAY
#define DISPLAY_W HEIGHT
#define DISPLAY_H WIDTH
#else
#define DISPLAY_W WIDTH
#define DISPLAY_H HEIGHT
#endif
#define FRAME_BYTES ((unsigned int)DISPLAY_W * DISPLAY_H * 4u)

#if HEIGHT % BAND_ROWS
#error HEIGHT must be a multiple of BAND_ROWS
#endif

void __in(int, Channel *, void *);
void __out(int, Channel *, const void *);

/* Patched by the linker with the end of the image (transputer-link.c). */
unsigned int __linker_end = 0x5a5a5a5au;
extern const unsigned char de_rom_image[], de_rom_image_end[];

void de_platform_write(const char *text, int length)
{
#if NODE == 0
    while (length-- > 0) UART = (unsigned char)*text++;
#else
    (void)text; (void)length;
#endif
}
int de_platform_heap(int segment, unsigned char **start, unsigned char **end)
{
    if (segment == 0) { *start = (unsigned char *)__linker_end; *end = (unsigned char *)0x81E00000u; return 1; }
    if (segment == 1) { *start = (unsigned char *)0x82000000u; *end = (unsigned char *)0x83E00000u; return 1; }
    return 0;
}
static void halt(void) { for (;;) ProcWait(TICKS_PER_SEC_LOW); }
static void uart_text(const char *s) { while (*s) UART = (unsigned char)*s++; }
static void uart_hex(unsigned int v) { int k; for (k = 28; k >= 0; k -= 4) UART = "0123456789abcdef"[(v >> k) & 15u]; }
/* Errors are reported from any node: a stuck tree is otherwise silent. */
void de_platform_fatal(const char *message)
{
    uart_text("DENGINE FATAL node "); uart_hex(NODE); UART = ' '; uart_text(message); UART = '\n';
    halt();
}
void de_platform_exit(int status) { uart_text("DENGINE EXIT node "); uart_hex(NODE); UART = ' '; uart_hex((unsigned)status); UART = '\n'; halt(); }
void de_platform_out_of_memory(size_t request)
{
    uart_text("DENGINE FATAL node "); uart_hex(NODE); uart_text(" out of memory: request "); uart_hex((unsigned)request);
    uart_text(" live "); uart_hex((unsigned)de_heap_live()); UART = '\n';
    halt();
}

static unsigned char *color;
static float *depth;
static unsigned char owned[HEIGHT];
static unsigned int band[BAND_WORDS];
static unsigned int header[4];   /* frame, band, checksum, render ticks */

static unsigned int checksum(const unsigned int *p, int n)
{
    unsigned int h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

/* Display rows (top first) of band t as ARGB words. */
static void pack_band(int t)
{
    int r, x;
    unsigned int *o = band;
    for (r = t * BAND_ROWS; r < (t + 1) * BAND_ROWS; r++) {
        const unsigned int *p = (const unsigned int *)(color + (size_t)(HEIGHT - 1 - r) * WIDTH * 4u);
        for (x = 0; x < WIDTH; x++) {
            unsigned int w = *p++;   /* bytes R G B A */
            *o++ = (w & 0xff00ff00u) | ((w >> 16) & 0xffu) | ((w & 0xffu) << 16);
        }
    }
}

static int subtree_bands(int root)
{
    int i, j, total = 0;
    for (i = root; i < NNODES; i++) {
        j = i;
        while (j > root) j = (j - 1) / 2;
        if (j == root && i < BANDS) total += (BANDS - 1 - i) / NNODES + 1;
    }
    return total;
}

#if NODE == 0
static int seen[BANDS], bad;
static unsigned int max_ticks, min_ticks;

static void accept(const unsigned int *h, const unsigned int *data, unsigned int frame, unsigned int base)
{
    int t = (int)h[1], r, x;
    if (t < 0 || t >= BANDS) { bad++; return; }
    if (h[0] != frame || h[2] != checksum(data, BAND_WORDS) || seen[t]) bad++;
    seen[t] = 1;
    if (h[3] > max_ticks) max_ticks = h[3];
    if (h[3] < min_ticks) min_ticks = h[3];
    for (r = 0; r < BAND_ROWS; r++) {
        int row = t * BAND_ROWS + r;
        const unsigned int *p = data + r * WIDTH;
#if ROTATE_DISPLAY
        /* Portrait (x, row) -> landscape (row, WIDTH-1-x): turned left. */
        unsigned int *col = (unsigned int *)(base + (unsigned int)row * 4u);
        for (x = 0; x < WIDTH; x++) col[(WIDTH - 1 - x) * DISPLAY_W] = p[x];
#else
        memcpy((void *)(base + (unsigned int)row * WIDTH * 4u), p, WIDTH * 4u);
#endif
    }
}
#endif

int test_main(void)
{
    de_app_settings s;
    unsigned int frame, t0;
    int y;
#if NODE == 0
    unsigned int init_ticks;
    REG(0xA0000004u) = FB_BASE; REG(0xA0000008u) = DISPLAY_W;
    REG(0xA000000cu) = DISPLAY_H; REG(0xA0000010u) = DISPLAY_W * 4u; REG(0xA0000000u) = 1;
#endif
    t0 = (unsigned int)Time();
    de_rom_mount(de_rom_image, (unsigned long)(de_rom_image_end - de_rom_image));
    color = malloc((size_t)WIDTH * HEIGHT * 4u);
    depth = malloc((size_t)WIDTH * HEIGHT * sizeof(float));
    if (!color || !depth) de_platform_fatal("no memory for the window");
    memset(color, 0, (size_t)WIDTH * HEIGHT * 4u);
    for (y = 0; y < HEIGHT; y++) owned[y] = ((HEIGHT - 1 - y) / BAND_ROWS) % NNODES == NODE;
    deGlSetWindow(WIDTH, HEIGHT, color, depth, NNODES > 1 ? owned : 0);
    memset(&s, 0, sizeof(s));
    s.width = WIDTH; s.height = HEIGHT; s.low_quality = LOW_QUALITY;
    s.shadows = SHADOWS; s.bump = BUMP; s.specular = SPECULAR; s.stats = STATS;
    s.camera_path = CAMERA_PATH; s.start_ms = START_MS; s.frame_ms = FRAME_MS;
    de_app_init(&s);
#if NODE == 0
    init_ticks = (unsigned int)Time() - t0;
    printf("DENGINE INIT nodes %d bands %d ticks %u heap peak %u live %u\n", NNODES, BANDS, init_ticks,
           (unsigned)de_heap_peak(), (unsigned)de_heap_live());
#endif
    for (frame = 0; frame < FRAMES; frame++) {
        int child, t;
        unsigned int render_start, render_ticks;
        de_gl_stats st;
#if NODE == 0
        unsigned int start = (unsigned int)Time(), elapsed;
        unsigned int base = FB_BASE + ((frame & 1u) ? 0u : FRAME_BYTES);
        for (t = 0; t < BANDS; t++) seen[t] = 0;
        max_ticks = 0; min_ticks = 0xffffffffu;
#else
        __in(sizeof(frame), LINKIN(0), &frame);
#endif
        for (child = 1; child <= 2; child++)
            if (NODE * 2 + child < NNODES) __out(sizeof(frame), LINKOUT(child), &frame);
        render_start = (unsigned int)Time();
        deGlStats(0, 1);
        de_app_frame();
        render_ticks = (unsigned int)Time() - render_start;
        deGlStats(&st, 0);
        for (t = NODE; t < BANDS; t += NNODES) {
            pack_band(t);
            header[0] = frame; header[1] = (unsigned int)t; header[2] = checksum(band, BAND_WORDS); header[3] = render_ticks;
#if NODE == 0
            accept(header, band, frame, base);
#else
            __out(sizeof(header), LINKOUT(0), header);
            __out(sizeof(band), LINKOUT(0), band);
#endif
        }
        for (child = 1; child <= 2; child++) {
            int root = NODE * 2 + child, n;
            if (root >= NNODES) continue;
            for (n = 0; n < subtree_bands(root); n++) {
                __in(sizeof(header), LINKIN(child), header);
                __in(sizeof(band), LINKIN(child), band);
#if NODE == 0
                accept(header, band, frame, base);
#else
                __out(sizeof(header), LINKOUT(0), header);
                __out(sizeof(band), LINKOUT(0), band);
#endif
            }
        }
#if NODE == 0
        for (t = 0; t < BANDS; t++) if (!seen[t]) bad++;
        elapsed = (unsigned int)Time() - start;
        REG(0xA0000014u) = base;
        printf("DENGINE FRAME %u t=%u ticks %u render %u max %u min %u draws %lu verts %lu tris %lu clipped %lu culled %lu frags %lu shaded %lu\n",
               frame, de_app_simulation_time(), elapsed, render_ticks, max_ticks, min_ticks, st.draws, st.vertices,
               st.triangles, st.clipped, st.culled, st.fragments, st.shaded);
#if CAPTURE
        REG(0xA0000018u) = 1;
#endif
#endif
    }
#if NODE == 0
    uart_text(bad ? "DENGINE BAD\n" : "DENGINE OK\n");
#endif
    halt();
    return 0;
}
