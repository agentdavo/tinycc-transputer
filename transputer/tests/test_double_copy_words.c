/* Copy the stored bits of an aligned double, including a signaling NaN.
 * Expected: Sraw:P local:P members:P self:P snapshot:P chain:P indexed:P volatile:P packed:P \nDOUBLE-COPY PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)

typedef union { double value; unsigned word[2]; } Bits;
typedef struct { Bits first, second; } Pair;
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

static void copy(double *dst, const double *src)
{
    *dst = *src;
}

static void copy_local(double *dst, unsigned lo, unsigned hi)
{
    Bits src;
    src.word[0] = lo;
    src.word[1] = hi;
    *dst = src.value;
}

static void copy_member(Pair *dst, const Pair *src)
{
    dst->second.value = src->first.value;
}

static double set_and_return(double previous, double *dst)
{
    *dst = -3.0;
    return previous;
}

static double snapshot(double *dst, const double *src)
{
    return set_and_return(*dst = *src, dst);
}

static double chain(double *dst, double *other, const double *src)
{
    return *dst = *other = *src;
}

static void indexed(Bits *dst, unsigned i, const Bits *src, unsigned j)
{
    dst[i].value = src[j].value;
}

static void copy_volatile(volatile double *dst, const volatile double *src)
{
    *dst = *src;
}

static void copy_packed(Packed *dst, const Packed *src)
{
    dst->value = src->value;
}

static int bits(const Bits *v, unsigned lo, unsigned hi)
{
    return v->word[0] == lo && v->word[1] == hi;
}

void test_main(void)
{
    Bits src, dst, out, other, table[3];
    Pair record, result;
    Packed p = { 'a', 7.25 }, q = { 'b', -3.5 };

    src.word[0] = 0x03141592;
    src.word[1] = 0x7ff00001;
    copy(&dst.value, &src.value);
    check("raw", bits(&dst, 0x03141592, 0x7ff00001));
    copy_local(&dst.value, 0, 0x80000000);
    check("local", bits(&dst, 0, 0x80000000));

    record.first.word[0] = 1;
    record.first.word[1] = 0;
    result.first.value = 2.0;
    copy_member(&result, &record);
    check("members", bits(&result.second, 1, 0) && result.first.value == 2.0);
    copy(&src.value, &src.value);
    check("self", bits(&src, 0x03141592, 0x7ff00001));

    out.value = snapshot(&dst.value, &src.value);
    check("snapshot", bits(&out, 0x03141592, 0x7ff00001) && dst.value == -3.0);
    out.value = chain(&dst.value, &other.value, &src.value);
    check("chain", bits(&out, 0x03141592, 0x7ff00001) &&
          bits(&dst, 0x03141592, 0x7ff00001) && bits(&other, 0x03141592, 0x7ff00001));

    table[0].value = 5.0;
    table[1].value = 6.0;
    table[2].word[0] = 0;
    table[2].word[1] = 0xfff00000;
    indexed(table, 1, table, 2);
    check("indexed", table[0].value == 5.0 && bits(&table[1], 0, 0xfff00000) &&
          bits(&table[2], 0, 0xfff00000));
    copy_volatile(&dst.value, &src.value);
    check("volatile", bits(&dst, 0x03141592, 0x7ff00001));
    copy_packed(&p, &q);
    check("packed", p.tag == 'a' && p.value == -3.5 && q.tag == 'b');
    put(good ? "\nDOUBLE-COPY PASS\n" : "\nDOUBLE-COPY FAIL\n");
}
