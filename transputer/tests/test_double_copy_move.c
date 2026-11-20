/* Expected: Sraw:P reassigned:P escaped:P overlap:P entry:P volatile:P gap:P \nDOUBLE-MOVE PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)

typedef union { double value; unsigned word[2]; } Bits;
typedef struct { Bits x, y, z; } Vector;
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

static int same(const Bits *a, const Bits *b)
{
    return a->word[0] == b->word[0] && a->word[1] == b->word[1];
}

static void fill(Bits *a, const Bits *b)
{
    int i;
    for (i = 0; i < 4; i++) {
        a[i].word[0] = b[i].word[0];
        a[i].word[1] = b[i].word[1];
    }
}

static void terminal(Vector *out, const Bits *in)
{
    Bits src[4];
    fill(src, in);
    out->x.value = src[0].value;
    out->y.value = src[1].value;
    out->z.value = src[2].value;
}

static void reassigned(Vector *out, Vector *other, const Bits *in, int use_other)
{
    Bits src[4];
    fill(src, in);
    if (use_other)
        out = other;
    out->x.value = src[0].value;
    out->y.value = src[1].value;
    out->z.value = src[2].value;
}

static void redirect(Vector **out, Vector *other)
{
    *out = other;
}

static void escaped(Vector *out, Vector *other, const Bits *in)
{
    Bits src[4];
    fill(src, in);
    redirect(&out, other);
    out->x.value = src[0].value;
    out->y.value = src[1].value;
    out->z.value = src[2].value;
}

static int overlap(const Bits *in)
{
    Bits src[4], *out;
    fill(src, in);
    out = src + 1;
    out[0].value = src[0].value;
    out[1].value = src[1].value;
    out[2].value = src[2].value;
    return same(src, src + 1) && same(src, src + 2) && same(src, src + 3);
}

static void entry(Vector *out, const Bits *in, int skip_first)
{
    Bits src[4];
    fill(src, in);
    if (skip_first)
        goto second;
    out->x.value = src[0].value;
second:
    out->y.value = src[1].value;
    out->z.value = src[2].value;
}

static void volatile_base(Vector *volatile out, const Bits *in)
{
    Bits src[4];
    fill(src, in);
    out->x.value = src[0].value;
    out->y.value = src[1].value;
    out->z.value = src[2].value;
}

static void gap(Vector *out, const Bits *in)
{
    Bits src[4];
    fill(src, in);
    out->x.value = src[0].value;
    out->y.value = src[1].value;
    out->z.value = src[3].value;
}

void test_main(void)
{
    Bits in[4];
    struct { Bits pre; Vector vec; Bits post; } out;
    Vector other;
    in[0].word[0] = 0x13456789;
    in[0].word[1] = 0x7ff00001;
    in[1].word[0] = 0;
    in[1].word[1] = 0x80000000;
    in[2].word[0] = 1;
    in[2].word[1] = 0;
    in[3].value = 4.25;
    out.pre.value = 5.5;
    out.post.value = 6.5;
    out.vec.x.value = 7.5;

    terminal(&out.vec, in);
    check("raw", out.pre.value == 5.5 && same(&out.vec.x, in) &&
          same(&out.vec.y, in + 1) && same(&out.vec.z, in + 2) && out.post.value == 6.5);

    out.vec.x.value = 7.5;
    reassigned(&out.vec, &other, in, 1);
    check("reassigned", out.vec.x.value == 7.5 && same(&other.x, in) &&
          same(&other.y, in + 1) && same(&other.z, in + 2));

    other.x.value = 0;
    escaped(&out.vec, &other, in);
    check("escaped", out.vec.x.value == 7.5 && same(&other.x, in) &&
          same(&other.y, in + 1) && same(&other.z, in + 2));
    check("overlap", overlap(in));

    entry(&out.vec, in, 1);
    check("entry", out.vec.x.value == 7.5 && same(&out.vec.y, in + 1) && same(&out.vec.z, in + 2));

    volatile_base(&out.vec, in);
    check("volatile", same(&out.vec.x, in) && same(&out.vec.y, in + 1) && same(&out.vec.z, in + 2));

    gap(&out.vec, in);
    check("gap", same(&out.vec.x, in) && same(&out.vec.y, in + 1) && same(&out.vec.z, in + 3));
    put(good ? "\nDOUBLE-MOVE PASS\n" : "\nDOUBLE-MOVE FAIL\n");
}
