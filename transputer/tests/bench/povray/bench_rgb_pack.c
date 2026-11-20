/* Both entry points include the actual shared header in separate objects. */
#include <stdio.h>
unsigned __ldtimer(void);
void pack_bytes(unsigned char *, const unsigned char *, unsigned);
void pack_move2d(unsigned char *, const unsigned char *, unsigned);
static unsigned char source[2576], bytes[1936], block[1936];

int main(int argc, char **argv)
{
    unsigned a, b, n, i, t, byte_time = 0, block_time = 0, bad = 0;
    (void)argc;
    (void)argv;
    for (i = 0; i < sizeof source; ++i)
        source[i] = (unsigned char)(i * 37 + (i >> 5));
    for (a = 0; a < 4; ++a) {
        for (b = 0; b < 4; ++b) {
            for (i = 0; i < sizeof bytes; ++i)
                bytes[i] = block[i] = 0xa5;
            t = __ldtimer();
            for (n = 0; n < 32; ++n)
                pack_bytes(bytes + b, source + a, 640);
            byte_time += __ldtimer() - t;
            t = __ldtimer();
            for (n = 0; n < 32; ++n)
                pack_move2d(block + b, source + a, 640);
            block_time += __ldtimer() - t;
            for (i = 0; i < sizeof bytes; ++i)
                if (bytes[i] != block[i])
                    ++bad;
        }
    }
    for (i = 0; i < sizeof source; ++i)
        if (source[i] != (unsigned char)(i * 37 + (i >> 5)))
            ++bad;
    pack_bytes(bytes, source, 0);
    pack_move2d(block, source, 0);
    for (i = 0; i < sizeof bytes; ++i)
        if (bytes[i] != block[i])
            ++bad;
    printf("pack bytes %u\npack move2d %u\npack fail %u\n", byte_time, block_time, bad);
    return bad != 0;
}
