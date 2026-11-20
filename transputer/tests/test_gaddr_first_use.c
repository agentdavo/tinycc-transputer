/* First-use address loads must dominate their later uses. Check fall-through,
 * branch targets, repeated blocks, volatile accesses and a wide frame.
 * Expected: Sconditional:P loop:P goto:P switch:P wide:P \nGADDR-USE PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)

static volatile unsigned total, mask, count, labeled, selected, wide;
static int good = 1;

static void put(const char *s)
{
    while (*s)
        UART = (unsigned char)*s++;
}

static void check(const char *name, int ok)
{
    put(name);
    put(ok ? ":P " : ":F ");
    if (!ok)
        good = 0;
}

static void conditional(int take, unsigned value)
{
    if (take) {
        total += value;
        mask ^= value;
    }
}

static void loop(unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        count += i + 1;
}

static unsigned skip_first(int skip)
{
    if (skip)
        goto second;
    labeled = 17;
second:
    return labeled;
}

static unsigned choose(int index)
{
    switch (index) {
    case 1: selected = 9; return 1;
    case 2: return selected;
    case 3: return 3;
    case 4: return 4;
    default: return 0;
    }
}

static unsigned wide_frame(int take, unsigned value)
{
    volatile unsigned char pad[1100];

    pad[1099] = 3;
    if (take)
        wide += value;
    return pad[1099];
}

void test_main(void)
{
    conditional(0, 7);
    check("conditional", total == 0 && mask == 0);
    conditional(1, 3);
    conditional(1, 5);
    if (total != 8 || mask != 6)
        good = 0;

    loop(0);
    loop(4);
    check("loop", count == 10);
    labeled = 11;
    check("goto", skip_first(1) == 11 && skip_first(0) == 17 &&
                  skip_first(1) == 17);
    selected = 5;
    check("switch", choose(2) == 5 && choose(1) == 1 && choose(2) == 9 &&
                    choose(4) == 4);
    check("wide", wide_frame(0, 7) == 3 && wide_frame(1, 4) == 3 && wide == 4);
    put(good ? "\nGADDR-USE PASS\n" : "\nGADDR-USE FAIL\n");
}
