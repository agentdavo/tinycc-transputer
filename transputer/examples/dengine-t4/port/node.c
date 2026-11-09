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
/* Split the shadow map's rows over the nodes too (gather_shadow). */
#ifndef SHADOW_SPLIT
#define SHADOW_SPLIT 1
#endif
#ifndef SHADOW_BAND
#define SHADOW_BAND 5
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

extern unsigned int shadowFBOId;   /* renderer_progr.c */

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

/* Transport. Bands are numbered 0..total-1 and band b belongs to node
 * b % NNODES. Each node sends its own bands and its whole subtree's up to
 * its parent as one message pair - four words per band (frame, band,
 * checksum, the drawing node's render ticks), then the bands' data in the
 * same order - so a link carries two messages a phase however many bands
 * cross it. t4 hands each message between node threads, and per-band
 * messages made that the cost of a frame at 16 and 32 nodes. */
static unsigned int current_frame, current_render_ticks;
static unsigned int frame_meta[4 * BANDS], *frame_data;
static unsigned int *shadow_meta;
static unsigned char *shadow_data, *shadow_src;
static unsigned int shadow_band_bytes;

static unsigned int checksum(const unsigned int *p, int n)
{
    unsigned int h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

/* Bands of `total` owned by the nodes of the subtree rooted at `root`. */
static int subtree_count(int root, int total)
{
    int i, j, count = 0;
    for (i = root; i < NNODES; i++) {
        j = i;
        while (j > root) j = (j - 1) / 2;
        if (j == root && i < total) count += (total - 1 - i) / NNODES + 1;
    }
    return count;
}

/* This node's bands and its subtree's, into meta/data; sent to the parent
 * unless this is node 0. Returns how many bands are there. */
static int collect(int total, unsigned int band_bytes, unsigned int *meta, unsigned char *data,
                   void (*own)(int band, unsigned char *dst, unsigned int *meta4))
{
    int n = 0, b, child;
    for (b = NODE; b < total; b += NNODES, n++) own(b, data + (size_t)n * band_bytes, meta + 4 * n);
    for (child = 1; child <= 2; child++) {
        int root = NODE * 2 + child, m;
        if (root >= NNODES) continue;
        m = subtree_count(root, total);
        __in(m * 16, LINKIN(child), meta + 4 * n);
        __in((int)((unsigned int)m * band_bytes), LINKIN(child), data + (size_t)n * band_bytes);
        n += m;
    }
#if NODE != 0
    __out(n * 16, LINKOUT(0), meta);
    __out((int)((unsigned int)n * band_bytes), LINKOUT(0), data);
#endif
    return n;
}

/* Display rows (top first) of window band t as ARGB words. */
static void own_frame_band(int t, unsigned char *dst, unsigned int *meta)
{
    int r, x;
    unsigned int *o = (unsigned int *)dst;
    for (r = t * BAND_ROWS; r < (t + 1) * BAND_ROWS; r++) {
        const unsigned int *p = (const unsigned int *)(color + (size_t)(HEIGHT - 1 - r) * WIDTH * 4u);
        for (x = 0; x < WIDTH; x++) {
            unsigned int w = *p++;   /* bytes R G B A */
            *o++ = (w & 0xff00ff00u) | ((w >> 16) & 0xffu) | ((w & 0xffu) << 16);
        }
    }
    meta[0] = current_frame; meta[1] = (unsigned int)t;
    meta[2] = checksum((const unsigned int *)dst, BAND_WORDS); meta[3] = current_render_ticks;
}

/* Rows of shadow-map band t, which this node drew. */
static void own_shadow_band(int t, unsigned char *dst, unsigned int *meta)
{
    memcpy(dst, shadow_src + (size_t)t * shadow_band_bytes, shadow_band_bytes);
    meta[0] = current_frame; meta[1] = (unsigned int)t;
    meta[2] = checksum((const unsigned int *)dst, (int)(shadow_band_bytes / 4u)); meta[3] = 0;
}

static unsigned int gather_ticks;
static int gather_bad;

/* The shadow map's other rows (deGlDistributeFramebuffer): every node's
 * bands go up to node 0, which then sends the whole map down, each node
 * passing it on to its children. */
static void gather_shadow(unsigned char *rgba, int w, int h, int band_rows)
{
    unsigned int start = (unsigned int)Time(), map_bytes = (unsigned int)w * (unsigned int)h * 4u;
    int bands = h / band_rows, n, k, child;
    shadow_band_bytes = (unsigned int)w * 4u * (unsigned int)band_rows;
    if (h % band_rows) de_platform_fatal("shadow map height is not a multiple of SHADOW_BAND");
    if (!shadow_data) {
        shadow_data = malloc(map_bytes);
        shadow_meta = malloc((size_t)bands * 16u);
        if (!shadow_data || !shadow_meta) de_platform_fatal("no memory for the shadow map exchange");
    }
    shadow_src = rgba;
    n = collect(bands, shadow_band_bytes, shadow_meta, shadow_data, own_shadow_band);
#if NODE == 0
    if (n != bands) gather_bad++;
    for (k = 0; k < n; k++) {
        const unsigned int *m = shadow_meta + 4 * k;
        const unsigned char *d = shadow_data + (size_t)k * shadow_band_bytes;
        if ((int)m[1] >= bands || m[0] != current_frame || m[2] != checksum((const unsigned int *)d, (int)(shadow_band_bytes / 4u))) { gather_bad++; continue; }
        if ((int)m[1] % NNODES != 0) memcpy(rgba + (size_t)m[1] * shadow_band_bytes, d, shadow_band_bytes);
    }
#else
    (void)k;
    __in((int)map_bytes, LINKIN(0), rgba);
#endif
    for (child = 1; child <= 2; child++)
        if (NODE * 2 + child < NNODES) __out((int)map_bytes, LINKOUT(child), rgba);
    (void)n;
    gather_ticks += (unsigned int)Time() - start;
}

#if NODE == 0
static int seen[BANDS], bad;
static unsigned int max_ticks, min_ticks;

static void accept(const unsigned int *meta, const unsigned int *data, unsigned int base)
{
    int t = (int)meta[1], r, x;
    if (t < 0 || t >= BANDS) { bad++; return; }
    if (meta[0] != current_frame || meta[2] != checksum(data, BAND_WORDS) || seen[t]) bad++;
    seen[t] = 1;
    if (meta[3] > max_ticks) max_ticks = meta[3];
    if (meta[3] < min_ticks) min_ticks = meta[3];
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
    frame_data = malloc((size_t)BANDS * BAND_WORDS * 4u);
    if (!color || !depth || !frame_data) de_platform_fatal("no memory for the window");
    memset(color, 0, (size_t)WIDTH * HEIGHT * 4u);
    for (y = 0; y < HEIGHT; y++) owned[y] = ((HEIGHT - 1 - y) / BAND_ROWS) % NNODES == NODE;
    deGlSetWindow(WIDTH, HEIGHT, color, depth, NNODES > 1 ? owned : 0);
    memset(&s, 0, sizeof(s));
    s.width = WIDTH; s.height = HEIGHT; s.low_quality = LOW_QUALITY;
    s.shadows = SHADOWS; s.bump = BUMP; s.specular = SPECULAR; s.stats = STATS;
    s.camera_path = CAMERA_PATH; s.start_ms = START_MS; s.frame_ms = FRAME_MS;
    de_app_init(&s);
#if NNODES > 1 && SHADOW_SPLIT
    if (!deGlDistributeFramebuffer(shadowFBOId, SHADOW_BAND, NNODES, NODE, gather_shadow))
        de_platform_fatal("cannot distribute the shadow framebuffer");
#endif
#if NODE == 0
    init_ticks = (unsigned int)Time() - t0;
    printf("DENGINE INIT nodes %d bands %d ticks %u heap peak %u live %u\n", NNODES, BANDS, init_ticks,
           (unsigned)de_heap_peak(), (unsigned)de_heap_live());
#endif
    for (frame = 0; frame < FRAMES; frame++) {
        int child, n;
        unsigned int render_start, render_ticks;
        de_gl_stats st;
#if NODE == 0
        unsigned int start = (unsigned int)Time(), elapsed, bands_start;
        unsigned int base = FB_BASE + ((frame & 1u) ? 0u : FRAME_BYTES);
        int t, k;
        for (t = 0; t < BANDS; t++) seen[t] = 0;
        max_ticks = 0; min_ticks = 0xffffffffu;
#else
        __in(sizeof(frame), LINKIN(0), &frame);
#endif
        for (child = 1; child <= 2; child++)
            if (NODE * 2 + child < NNODES) __out(sizeof(frame), LINKOUT(child), &frame);
        current_frame = frame;
        gather_ticks = 0;
        render_start = (unsigned int)Time();
        deGlStats(0, 1);
        de_app_frame();
        render_ticks = (unsigned int)Time() - render_start;
        deGlStats(&st, 0);
        current_render_ticks = render_ticks;
#if NODE == 0
        bands_start = (unsigned int)Time();
#endif
        n = collect(BANDS, BAND_WORDS * 4u, frame_meta, (unsigned char *)frame_data, own_frame_band);
#if NODE == 0
        if (n != BANDS) bad++;
        for (k = 0; k < n; k++) accept(frame_meta + 4 * k, frame_data + (size_t)k * BAND_WORDS, base);
        for (t = 0; t < BANDS; t++) if (!seen[t]) bad++;
        elapsed = (unsigned int)Time() - start;
        REG(0xA0000014u) = base;
        printf("DENGINE FRAME %u t=%u ticks %u render %u gather %u bands %u max %u min %u draws %lu verts %lu tris %lu clipped %lu culled %lu frags %lu shaded %lu\n",
               frame, de_app_simulation_time(), elapsed, render_ticks, gather_ticks, (unsigned int)Time() - bands_start,
               max_ticks, min_ticks, st.draws, st.vertices, st.triangles, st.clipped, st.culled, st.fragments, st.shaded);
#if CAPTURE
        REG(0xA0000018u) = 1;
#endif
#else
        (void)n;
#endif
    }
#if NODE == 0
    uart_text(bad || gather_bad ? "DENGINE BAD\n" : "DENGINE OK\n");
#endif
#ifdef PROFILE_EXIT
    /* Stop the emulator (t4 -se) so its IPtr samples are written out. */
    __seterr();
#endif
    halt();
    return 0;
}
