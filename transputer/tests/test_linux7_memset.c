/* Expected: SFILLS PASS\nH */
typedef unsigned __kernel_size_t;
#ifndef FILL_HEADER
#define FILL_HEADER "../linux-7/arch/transputer/include/asm/string.h"
#endif
#include FILL_HEADER

#define UART (*(volatile unsigned *)0x10000000u)
static unsigned char buffer[20480] __attribute__((aligned(4096)));
static unsigned failures;
static unsigned calls;

void clear_page_move(void *page)
{
    unsigned *p = page;
    for (unsigned i = 0; i < 1024; ++i)
        p[i] = 0;
}

static void check(unsigned start, unsigned count, int value)
{
    volatile unsigned char *bytes = buffer;
    unsigned end = start + count + 8;

    for (unsigned i = start - 8; i < end; ++i)
        bytes[i] = 0xa5;
    if (memset(buffer + start, value, count) != buffer + start)
        failures++;
    for (unsigned i = start - 8; i < end; ++i) {
        unsigned expected = i < start || i >= start + count ? 0xa5 : (unsigned char)value;
        if (bytes[i] != expected)
            failures++;
    }
    calls++;
}

void test_main(void)
{
    static const unsigned lengths[] = {
        95, 127, 128, 129, 255, 256, 257, 511, 512, 513,
        1023, 1024, 1025, 4095, 4096, 4097, 8191, 8192, 8193
    };
    static const int values[] = { 0, 0x100, -1, 0x123, -129 };
    const char *message;

    for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v)
        for (unsigned offset = 0; offset < 4; ++offset) {
            for (unsigned size = 0; size <= 81; ++size)
                check(4096 + offset, size, values[v]);
            for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
                check(4096 + offset, lengths[i], values[v]);
        }
    message = failures || calls != 2020 ? "FILLS FAIL\n" : "FILLS PASS\n";
    while (*message)
        UART = (unsigned char)*message++;
}