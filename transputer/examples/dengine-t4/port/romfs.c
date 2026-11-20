/* Read-only file lookup in the image written by mkrom.py. */
#include "platform.h"
#include "include/string.h"

static const unsigned char *rom;
static unsigned long rom_size;
static unsigned int rom_count;

static unsigned int word(const unsigned char *p)
{
    return (unsigned int)p[0] | (unsigned int)p[1] << 8 | (unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}

void de_rom_mount(const unsigned char *image, unsigned long size)
{
    rom = 0; rom_size = 0; rom_count = 0;
    if (!image || size < 16 || memcmp(image, "DROM", 4) || word(image + 4) != 1 || word(image + 12) > size) {
        de_platform_fatal("ROM image missing or corrupt");
        return;
    }
    rom = image; rom_size = size; rom_count = word(image + 8);
}

unsigned int de_rom_file_count(void) { return rom_count; }

const unsigned char *de_rom_open(const char *path, unsigned long *size)
{
    unsigned int lo = 0, hi = rom_count;
    if (!rom || !path) return 0;
    for (;;) {
        if (path[0] == '.' && path[1] == '/') path += 2;
        else if (path[0] == '/') path++;
        else break;
    }
    while (lo < hi) {
        unsigned int mid = (lo + hi) / 2;
        const unsigned char *e = rom + 16 + 16u * mid;
        int c = strcmp(path, (const char *)rom + word(e));
        if (!c) {
            if (size) *size = word(e + 12);
            return rom + word(e + 8);
        }
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return 0;
}
