/* Expected: Slocal:P array:P alias:P loop:P used:P nested:P volatile:P packed:P \nDOUBLE-STACK PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)

typedef union { double value; unsigned word[2]; } Bits;
typedef struct __attribute__((packed)) { char tag; double value; } Packed;
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

static int same_bits(const Bits *value, unsigned lo, unsigned hi)
{
    return value->word[0] == lo && value->word[1] == hi;
}

static void local_copy(double *dst, unsigned lo, unsigned hi)
{
    Bits value;
    value.word[0] = lo;
    value.word[1] = hi;
    *dst = value.value;
}

static void array_copy(Bits *dst, unsigned lo, unsigned hi)
{
    Bits a[2];
    a[1].word[0] = lo;
    a[1].word[1] = hi;
    dst[1].value = a[1].value;
}

static double *choose(int self, double *src, double *dst)
{
    return self ? src : dst;
}

static int alias_copy(double *dst, int self, unsigned lo, unsigned hi)
{
    Bits src;
    double *p = choose(self, &src.value, dst);
    src.word[0] = lo;
    src.word[1] = hi;
    *p = src.value;
    return same_bits(&src, lo, hi);
}

static void loop_copy(Bits *dst, int n)
{
    Bits src = { 2.25 };
    int i;
    for (i = 0; i < n; i++, dst[i - 1].value = src.value)
        dst[i].word[0] = 0;
}

static double overwrite(double *dst, double result)
{
    *dst = 1.0;
    return result;
}

static double used_copy(double *dst, unsigned lo, unsigned hi)
{
    Bits src;
    src.word[0] = lo;
    src.word[1] = hi;
    return overwrite(dst, *dst = src.value);
}

static int nested_copy(double *dst, int i)
{
    Bits src = { 3.5 };
    return i + ({ *dst = src.value; 5; });
}

static void volatile_copy(double *dst, double value)
{
    volatile double src = value;
    *dst = src;
}

static void packed_copy(double *dst, double value)
{
    Packed src = { 'x', value };
    *dst = src.value;
}

void test_main(void)
{
    Bits dst[3], result;
    unsigned lo = 0x13456789, hi = 0x7ff00001;

    local_copy(&dst[0].value, lo, hi);
    check("local", same_bits(&dst[0], lo, hi));
    array_copy(dst, 0, 0x80000000);
    check("array", same_bits(&dst[1], 0, 0x80000000) && same_bits(&dst[0], lo, hi));
    check("alias", alias_copy(&dst[0].value, 1, 1, 0) && same_bits(&dst[0], lo, hi) &&
          alias_copy(&dst[0].value, 0, 1, 0) && same_bits(&dst[0], 1, 0));

    dst[2].value = 6.0;
    loop_copy(dst, 0);
    check("loop", same_bits(&dst[0], 1, 0) &&
          (loop_copy(dst, 2), dst[0].value == 2.25 && dst[1].value == 2.25 && dst[2].value == 6.0));

    result.value = used_copy(&dst[0].value, lo, hi);
    check("used", same_bits(&result, lo, hi) && dst[0].value == 1.0);
    check("nested", nested_copy(&dst[0].value, 22) == 27 && dst[0].value == 3.5);
    volatile_copy(&dst[0].value, -2.75);
    check("volatile", dst[0].value == -2.75);
    packed_copy(&dst[0].value, 7.25);
    check("packed", dst[0].value == 7.25);
    put(good ? "\nDOUBLE-STACK PASS\n" : "\nDOUBLE-STACK FAIL\n");
}
