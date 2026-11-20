/* Expected: Sreturn:P fourth:P entry:P escaped:P overlap:P \nDOUBLE-NONTERMINAL PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)

typedef union { double value; unsigned word[2]; } Bits;
typedef struct { Bits x, y, z, w; } Vector;
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

static int copy_then_return(Vector *out, const Bits *in)
{
    Bits src[4];
    fill(src, in);
    out->x.value = src[0].value;
    out->y.value = src[1].value;
    out->z.value = src[2].value;
    return src[3].word[0] + 17;
}

static void copy_fourth(int tag, const Bits *in, int unused, Vector *out)
{
    Bits src[4];
    fill(src, in);
    src[3].word[0] += tag;
    out->x.value = src[0].value;
    out->y.value = src[1].value;
    out->z.value = src[2].value;
    out->w.value = src[3].value;
}

static int middle_entry(Vector *out, const Bits *in, int skip_first)
{
    Bits src[4];
    fill(src, in);
    if (skip_first)
        goto second;
    out->x.value = src[0].value;
second:
    out->y.value = src[1].value;
    out->z.value = src[2].value;
    return src[3].word[0] + 17;
}

static void redirect(Vector **out, Vector *other)
{
    *out = other;
}

static int escaped(Vector *out, Vector *other, const Bits *in)
{
    Bits src[4];
    fill(src, in);
    redirect(&out, other);
    out->x.value = src[0].value;
    out->y.value = src[1].value;
    out->z.value = src[2].value;
    return src[3].word[0] + 17;
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

void test_main(void)
{
    Bits in[4];
    Vector out, other;
    int result;

    in[0].word[0] = 0x13456789;
    in[0].word[1] = 0x7ff00001;
    in[1].word[0] = 0;
    in[1].word[1] = 0x80000000;
    in[2].word[0] = 1;
    in[2].word[1] = 0;
    in[3].word[0] = 12;
    in[3].word[1] = 0x40100000;

    out.w.value = 6.5;
    result = copy_then_return(&out, in);
    check("return", result == 29 && same(&out.x, in) && same(&out.y, in + 1) &&
          same(&out.z, in + 2) && out.w.value == 6.5);

    copy_fourth(3, in, 0, &out);
    check("fourth", same(&out.x, in) && same(&out.y, in + 1) && same(&out.z, in + 2) &&
          out.w.word[0] == 15 && out.w.word[1] == 0x40100000);

    out.x.value = 7.5;
    result = middle_entry(&out, in, 1);
    check("entry", result == 29 && out.x.value == 7.5 &&
          same(&out.y, in + 1) && same(&out.z, in + 2));

    result = escaped(&out, &other, in);
    check("escaped", result == 29 && out.x.value == 7.5 &&
          same(&other.x, in) && same(&other.y, in + 1) && same(&other.z, in + 2));
    check("overlap", overlap(in));
    put(good ? "\nDOUBLE-NONTERMINAL PASS\n" : "\nDOUBLE-NONTERMINAL FAIL\n");
}
