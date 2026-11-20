/* Expected: SROTATE32 PASS\nH */

#define UART (*(volatile unsigned *)0x10000000u)

static volatile unsigned word = 0x89abcdefu;
static volatile unsigned count;

static unsigned get_word(void)
{
    return word;
}

void test_main(void)
{
    unsigned value = word;
    int ok = 1;
    const char *message;

    ok &= __builtin_rotateleft32(0x89abcdefu, 4) == 0x9abcdef8u;
    ok &= __builtin_rotateleft32(word, 32) == value;
    ok &= __builtin_rotateleft32(word, 16) == 0xcdef89abu;
    ok &= __builtin_rotateleft32(word, 25) == 0xdf13579bu;
    ok &= __builtin_rotateright32(word, 7) == 0xdf13579bu;
    ok &= __builtin_rotateright32(word, 24) == 0xabcdef89u;
    ok &= __builtin_rotateright32(word, -4) == 0x9abcdef8u;
    ok &= (get_word() ^ __builtin_rotateleft32(get_word(), 16)) == 0x44444444u;

    for (unsigned i = 0; i < 65; i++) {
        unsigned n = i & 31;
        unsigned left = (value << n) | (value >> ((-n) & 31));
        unsigned right = (value >> n) | (value << ((-n) & 31));

        count = i;
        ok &= __builtin_rotateleft32(word, count) == left;
        ok &= __builtin_rotateright32(word, count) == right;
    }
    unsigned i = 7;
    ok &= __builtin_rotateleft32(word, i++) == 0xd5e6f7c4u && i == 8;
    message = ok ? "ROTATE32 PASS\n" : "ROTATE32 FAIL\n";
    while (*message)
        UART = (unsigned char)*message++;
}
