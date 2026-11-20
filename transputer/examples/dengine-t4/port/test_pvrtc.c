/* Host check of the PVRTC decoder: decodes a PVR v2 file and compares it
 * with the PNG it was made from (same size), reporting the mean absolute
 * RGB error. A wrong block order or modulation layout gives an error of
 * tens of levels; PVRTC itself costs a few. */
#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/string.h"
#include "platform.h"
#include "png.h"
#include "pvrtc.h"

int open(const char *path, int flags, ...);
long read(int fd, void *buf, unsigned long n);
long write(int fd, const void *buf, unsigned long n);
int close(int fd);
void _exit(int status);

static unsigned char heap[256u << 20];
void de_platform_write(const char *text, int length) { write(1, text, (unsigned long)length); }
int de_platform_heap(int segment, unsigned char **start, unsigned char **end) { if (segment) return 0; *start = heap; *end = heap + sizeof(heap); return 1; }
void de_platform_exit(int status) { fflush(stdout); _exit(status); }
void de_platform_fatal(const char *message) { printf("FATAL: %s\n", message); fflush(stdout); _exit(2); }
void de_platform_out_of_memory(size_t request) { printf("FATAL: out of memory %lu\n", (unsigned long)request); fflush(stdout); _exit(3); }
unsigned int de_platform_time_ms(void) { return 0; }

static unsigned char *load(const char *path, unsigned long *size)
{
    int fd = open(path, 0);
    unsigned long cap = 1u << 22, n = 0;
    unsigned char *buf = malloc(cap);
    long got;
    if (fd < 0) return 0;
    while ((got = read(fd, buf + n, cap - n)) > 0) n += (unsigned long)got;
    close(fd);
    *size = n;
    return buf;
}

static unsigned int le32(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

int main(int argc, char **argv)
{
    unsigned long psize, nsize, i, n, err = 0;
    unsigned char *pvr, *png, *out;
    de_png_image img;
    unsigned int w, h, flags, bpp;
    if (argc < 3) { printf("usage: test_pvrtc file.pvr file.png [out.raw]\n"); return 1; }
    pvr = load(argv[1], &psize); png = load(argv[2], &nsize);
    if (!pvr || !png || de_png_decode(png, nsize, &img)) { printf("cannot load inputs\n"); return 1; }
    h = le32(pvr + 4); w = le32(pvr + 8); flags = le32(pvr + 16); bpp = le32(pvr + 24);
    if (w != img.width || h != img.height) { printf("size mismatch %ux%u vs %ux%u\n", w, h, img.width, img.height); return 1; }
    out = malloc((size_t)w * h * 4u);
    if (!de_pvrtc_decode(pvr + 52, w, h, bpp == 2, 1, out)) { printf("decode failed\n"); return 1; }
    n = (unsigned long)w * h;
    for (i = 0; i < n; i++) {
        int k;
        for (k = 0; k < 3; k++) { int d = (int)out[i * 4 + k] - (int)img.rgba[i * 4 + k]; err += (unsigned long)(d < 0 ? -d : d); }
    }
    printf("%s: %ux%u flags %#x bpp %u mean |error| %.2f\n", argv[1], w, h, flags, bpp, (double)err / (double)(n * 3));
    if (argc > 3) { int fd = open(argv[3], 01101, 0644); write(fd, out, n * 4u); close(fd); }
    return 0;
}
