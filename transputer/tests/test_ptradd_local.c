/* Expected: SLOCALIDXOK\nH */
static volatile int index_one = 1;

void test_main(void)
{
    struct {
        unsigned char guard, bytes[4], tail;
        short halves[3];
        int words[3];
    } a = { 17, { 1, 2, 3, 4 }, 19, { 5, 6, 7 }, { 8, 9, 10 } };
    volatile unsigned *uart = (volatile unsigned *)0x10000000u;
    const char *msg;
    int i = index_one;

    /* bytes starts one byte into a word, halves two bytes into another.
       Check with literal subscripts so the reads use folded offsets. */
    a.bytes[i] = a.bytes[i + 1] + 10;
    a.halves[i] = a.halves[i - 1] + 20;
    a.words[i++] = a.words[2] + 30;
    msg = i == 2 && a.guard == 17 && a.tail == 19 &&
          a.bytes[0] == 1 && a.bytes[1] == 13 && a.bytes[2] == 3 &&
          a.halves[0] == 5 && a.halves[1] == 25 && a.halves[2] == 7 &&
          a.words[0] == 8 && a.words[1] == 40 && a.words[2] == 10
          ? "LOCALIDXOK\n" : "LOCALIDXBAD\n";
    while (*msg)
        *uart = *msg++;
}
