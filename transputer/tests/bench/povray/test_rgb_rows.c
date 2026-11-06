/* Replay the historical wire layout at every byte alignment.  The oracle
 * is an independently supplied packaged PPM, never the packing routine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rgbrow.h"

#ifdef __TRANSPUTER__
long __transputer_syscall(long, long, long, long, long, long, long);
static int check_seek(FILE *file)
{
    unsigned long result[2] = {0x12345678u, 0xabcdef01u};
    int fd = fileno(file);
    if (__transputer_syscall(62, fd, 1, 0, (long)result, SEEK_SET, 0) != -75 ||
        result[0] != 0x12345678u || result[1] != 0xabcdef01u)
        return 0;
    if (__transputer_syscall(62, fd, 0, 15, (long)result, SEEK_SET, 0) ||
        result[0] != 15 || result[1] != 0)
        return 0;
    if (__transputer_syscall(62, fd, -1, -1, (long)result, SEEK_CUR, 0) ||
        result[0] != 14 || result[1] != 0)
        return 0;
    if (__transputer_syscall(62, fd, 0, 0, (long)result, 99, 0) != -22 ||
        result[0] != 14 || result[1] != 0)
        return 0;
    return __transputer_syscall(1057, fd, 0, SEEK_END, 0, 0, 0) == 921615;
}
#endif

int main(int argc, char **argv)
{
    unsigned char expected[1920], actual[1922], packet[2576];
    const char *names[] = {"rows0.ppm", "rows1.ppm", "rows2.ppm", "rows3.ppm"};
    FILE *reference = fopen("reference.ppm", "rb");
    struct pov_rgb_writer output;
    unsigned phase, n, x, y;
    FILE *file;
    (void)argc;
    (void)argv;
    if (!reference)
        return 1;
#ifdef __TRANSPUTER__
    if (!check_seek(reference))
        return 7;
#endif
    for (phase = 0; phase < 4; ++phase) {
        file = fopen(names[phase], "wb");
        if (!file || !pov_rgb_begin(&output, file, 640, 480))
            return 2;
        for (n = 0; n < 480; ++n) {
            y = phase == 0 ? n : phase == 1 ? 479 - n : (n * 31) % 480;
            if (fseek(reference, 15L + (long)y * 1920, SEEK_SET) ||
                fread(expected, 1, 1920, reference) != 1920)
                return 3;
            memset(packet, 0xa7, sizeof packet);
            for (x = 0; x < 640; ++x) {
                unsigned char *p = packet + phase + 9 + x * 4;
                p[0] = expected[x * 3];
                p[1] = expected[x * 3 + 1];
                p[2] = expected[x * 3 + 2];
                p[3] = 0xa5;     /* Padding must never enter the RGB file. */
            }
            actual[0] = actual[1921] = 0x5a;
            pov_rgb_unpack(actual + 1, packet + phase + 9, 640);
            for (x = 0; x < 640; ++x) {
                unsigned char *p = packet + phase + 9 + x * 4;
                if (memcmp(p, expected + x * 3, 3) || p[3] != 0xa5)
                    return 4;
            }
            for (x = 0; x < phase + 9; ++x)
                if (packet[x] != 0xa7)
                    return 4;
            for (x = phase + 9 + 640 * 4; x < sizeof packet; ++x)
                if (packet[x] != 0xa7)
                    return 4;
            if (actual[0] != 0x5a || actual[1921] != 0x5a ||
                memcmp(actual + 1, expected, 1920) ||
                !pov_rgb_write(&output, actual + 1, y))
                return 4;
        }
        if (!pov_rgb_write(&output, actual + 1, y) ||
            pov_rgb_write(&output, actual + 1, 480) || fclose(file))
            return 5;
    }
    if (fclose(reference))
        return 6;
    puts("ROWTEST PASS");
    return 0;
}
