/* Host driver for the dEngine port: runs the same engine, libc and GL code
 * as a T4 node and writes each frame as a PPM. With --nodes/--node it draws
 * only that node's rows, so per-node images can be composed and compared
 * with a one-node frame before going near the emulator. */
#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/string.h"
#include "platform.h"
#include "gles2_port.h"
#include "dengine_app.h"

/* POSIX, declared here because the port's headers replace the C library's. */
int open(const char *path, int flags, ...);
long read(int fd, void *buf, unsigned long n);
long write(int fd, const void *buf, unsigned long n);
int close(int fd);
void _exit(int status);
#define HOST_O_RDONLY 0
#define HOST_O_WRONLY 1
#define HOST_O_CREAT 0100
#define HOST_O_TRUNC 01000

#define HEAP_BYTES (512u << 20)
static unsigned char heap[HEAP_BYTES];

void de_platform_write(const char *text, int length) { write(1, text, (unsigned long)length); }
int de_platform_heap(int segment, unsigned char **start, unsigned char **end) { if (segment) return 0; *start = heap; *end = heap + HEAP_BYTES; return 1; }
void de_platform_exit(int status) { fflush(stdout); _exit(status); }
void de_platform_fatal(const char *message) { printf("FATAL: %s\n", message); fflush(stdout); _exit(2); }
void de_platform_out_of_memory(size_t request) { printf("FATAL: out of memory (%lu bytes, %lu live)\n", (unsigned long)request, (unsigned long)de_heap_live()); fflush(stdout); _exit(3); }

static unsigned char *load(const char *path, unsigned long *size)
{
    int fd = open(path, HOST_O_RDONLY);
    unsigned long cap = 1u << 20, n = 0;
    unsigned char *buf = malloc(cap);
    long got;
    if (fd < 0 || !buf) return 0;
    for (;;) {
        if (n == cap) { cap *= 2; buf = realloc(buf, cap); if (!buf) return 0; }
        got = read(fd, buf + n, cap - n);
        if (got <= 0) break;
        n += (unsigned long)got;
    }
    close(fd);
    *size = n;
    return buf;
}

static void write_ppm(const char *path, const unsigned char *rgba, int w, int h)
{
    int fd = open(path, HOST_O_WRONLY | HOST_O_CREAT | HOST_O_TRUNC, 0644), x, y;
    char header[64];
    unsigned char *row = malloc((size_t)w * 3u);
    if (fd < 0 || !row) { printf("cannot write %s\n", path); return; }
    sprintf(header, "P6\n%d %d\n255\n", w, h);
    write(fd, header, strlen(header));
    for (y = h - 1; y >= 0; y--) {          /* GL rows are bottom first */
        const unsigned char *p = rgba + (size_t)y * (size_t)w * 4u;
        for (x = 0; x < w; x++) { row[x * 3] = p[x * 4]; row[x * 3 + 1] = p[x * 4 + 1]; row[x * 3 + 2] = p[x * 4 + 2]; }
        write(fd, row, (unsigned long)w * 3u);
    }
    close(fd);
    free(row);
}

int main(int argc, char **argv)
{
    de_app_settings s;
    const char *rom_path = "rom.bin", *out = "frame";
    int frames = 1, nodes = 1, node = 0, band = 5, i, y;
    unsigned long rom_size = 0;
    unsigned char *rom, *color, *owned;
    float *depth;
    memset(&s, 0, sizeof(s));
    s.width = 320; s.height = 480; s.shadows = s.bump = s.specular = 1; s.camera_path = 1;
    s.frame_ms = 33;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "0";
        if (!strcmp(a, "--rom")) { rom_path = v; i++; }
        else if (!strcmp(a, "--out")) { out = v; i++; }
        else if (!strcmp(a, "--frames")) { frames = atoi(v); i++; }
        else if (!strcmp(a, "--width")) { s.width = atoi(v); i++; }
        else if (!strcmp(a, "--height")) { s.height = atoi(v); i++; }
        else if (!strcmp(a, "--start")) { s.start_ms = (unsigned)atoi(v); i++; }
        else if (!strcmp(a, "--frame-ms")) { s.frame_ms = (unsigned)atoi(v); i++; }
        else if (!strcmp(a, "--nodes")) { nodes = atoi(v); i++; }
        else if (!strcmp(a, "--node")) { node = atoi(v); i++; }
        else if (!strcmp(a, "--band")) { band = atoi(v); i++; }
        else if (!strcmp(a, "--low")) s.low_quality = 1;
        else if (!strcmp(a, "--no-shadow")) s.shadows = 0;
        else if (!strcmp(a, "--no-bump")) s.bump = 0;
        else if (!strcmp(a, "--no-spec")) s.specular = 0;
        else if (!strcmp(a, "--stats")) s.stats = 1;
        else if (!strcmp(a, "--no-camera")) s.camera_path = 0;
        else { printf("unknown option %s\n", a); return 1; }
    }
    rom = load(rom_path, &rom_size);
    if (!rom) { printf("cannot read ROM %s\n", rom_path); return 1; }
    de_rom_mount(rom, rom_size);
    color = calloc((size_t)s.width * (size_t)s.height, 4u);
    depth = calloc((size_t)s.width * (size_t)s.height, sizeof(float));
    owned = calloc((size_t)s.height, 1u);
    for (y = 0; y < s.height; y++) owned[y] = ((s.height - 1 - y) / band) % nodes == node;
    deGlSetWindow(s.width, s.height, color, depth, nodes > 1 ? owned : 0);
    de_app_init(&s);
    printf("[host] init done: heap peak %lu bytes, live %lu\n", (unsigned long)de_heap_peak(), (unsigned long)de_heap_live());
    for (i = 0; i < frames; i++) {
        char path[256];
        de_gl_stats st;
        deGlStats(0, 1);
        de_app_frame();
        deGlStats(&st, 0);
        sprintf(path, "%s_%03d.ppm", out, i);
        write_ppm(path, color, s.width, s.height);
        printf("[host] frame %d t=%u draws %lu tris %lu clipped %lu culled %lu verts %lu frags %lu zfail %lu shaded %lu\n",
               i, de_app_simulation_time(), st.draws, st.triangles, st.clipped, st.culled, st.vertices, st.fragments, st.depth_rejected, st.shaded);
    }
    fflush(stdout);
    return 0;
}
