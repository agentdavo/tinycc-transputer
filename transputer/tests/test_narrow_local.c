/* Expected: SNARROWLOCALOK\nH */
/*
 * test_narrow_local.c - char and short locals kept as whole words.
 *
 * tp_narrow_locals (transputer-fnopt.h) turns a char or short local that
 * nothing else names into a word holding the value extended to 32 bits:
 * reads become `ldl`, writes `stl` after a truncation unless the value is
 * known to be in range.  Each case here would come out wrong if a write
 * were left untruncated or a range were guessed too wide: wrap-around of
 * every kind, a parameter the caller passes with stray high bits (through
 * a pointer to a function declared with an int parameter), signed and
 * unsigned reads of one local, a shift of a value that is not provably
 * non-negative, a local whose address escapes (left alone), loops, and
 * CoreMark's crcu8 against a table computed on the host.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void uh(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int n = (v >> i) & 0xF;
        up(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

static int bad;
static void chk(int line, unsigned got, unsigned want)
{
    if (got != want) {
        bad++;
        us("\nFAIL line ");
        uh((unsigned)line);
        us(" got ");
        uh(got);
        us(" want ");
        uh(want);
    }
}
#define CHK(g, w) chk(__LINE__, (unsigned)(g), (unsigned)(w))

static int n_global = 300;

/* Wrap-around of each kind in a loop. */
static unsigned wrap_u8(int n)
{
    unsigned char c = 250;
    int i;
    for (i = 0; i < n; i++)
        c += 3;
    return c;
}
static int wrap_s8(int n)
{
    signed char c = 120;
    int i;
    for (i = 0; i < n; i++)
        c += 5;
    return c;
}
static unsigned wrap_u16(int n)
{
    unsigned short u = 65530;
    int i;
    for (i = 0; i < n; i++)
        u = u * 3 + 7;
    return u;
}
static int wrap_s16(int n)
{
    short s = 32000;
    int i;
    for (i = 0; i < n; i++)
        s += 1000;
    return s;
}

/* Parameters: the caller's word may carry high bits. */
static unsigned par_u8(unsigned char c) { unsigned char d = c; d++; return d + c; }
static int par_s8(signed char c) { signed char d = c; return d * 2; }
static unsigned par_u16(unsigned short c) { unsigned short d = c; return d + 1; }
static int par_s16(short c) { short d = c; return d - 1; }

/* Signed and unsigned reads of one local. */
static int mixed(int x)
{
    unsigned char c = (unsigned char)x;
    unsigned short h = (unsigned short)x;
    return (signed char)c + (short)h + c + h;
}

/* Values whose range the pass must not over-estimate. */
static unsigned ranges(int x, unsigned y)
{
    unsigned char a = x & 0x1ff;        /* mask wider than the type */
    unsigned char b = (unsigned)x >> 20; /* shift of a negative int */
    unsigned char c = x > 0;            /* a comparison: fits */
    unsigned short d = (y & 0xff) * (y & 0xff) * 2; /* product too big */
    signed char e = (y & 0xff);         /* 0..255 does not fit -128..127 */
    unsigned char f = a ^ c;
    return a + (b << 8) + (c << 16) + (d << 1) + ((unsigned)e << 20) + f;
}

/* A local whose address escapes keeps its byte accesses. */
static void poke(unsigned char *p) { *p = 0xA5; }
static unsigned escaped(void)
{
    unsigned char c = 3;
    unsigned char d = 9;
    poke(&c);
    return c + d;
}

/* CoreMark's crcu8, checked against a table made on the host. */
static unsigned short crcu8(unsigned char data, unsigned short crc)
{
    unsigned char i = 0, x16 = 0, carry = 0;

    for (i = 0; i < 8; i++) {
        x16 = (unsigned char)((data & 1) ^ ((unsigned char)crc & 1));
        data >>= 1;
        if (x16 == 1) {
            crc ^= 0x4002;
            carry = 1;
        } else
            carry = 0;
        crc >>= 1;
        if (carry)
            crc |= 0x8000;
        else
            crc &= 0x7fff;
    }
    return crc;
}

/* The low byte of a short, masked further and not. */
static unsigned lowbyte(unsigned short h0, int n)
{
    unsigned short h = h0;
    short g = (short)h0;
    unsigned t = 0;
    int i;
    for (i = 0; i < n; i++) {
        t += (unsigned char)h & 3;
        t = t * 3 + (unsigned char)h + (unsigned char)g;
        h += 0x155;
        g -= 0x123;
    }
    return t;
}

/* Shifts and masks the value ranges make shorter, and ones they must not:
 * a negative short shifts arithmetically, a mask of a value that may be
 * wider stays. */
static int shifts(int x, unsigned char u, short s16)
{
    unsigned char a = u;
    short b = s16;
    unsigned short c = (unsigned short)x;
    int t = (a >> 1) << 16;
    t += b >> 2;
    t += ((c >> 3) & 0x1fff) << 4;
    t += (x & 0x1ff) & 0xff;
    t += (a & 0xff) * 3;
    return t;
}

/* Narrow locals live across a call. */
static int keep_across(int x)
{
    signed char c = (signed char)x;
    unsigned short h = (unsigned short)(x * 7);
    int t = par_s8(c);
    return t + c + h;
}

void test_main(void)
{
    unsigned (*pu)(int) = (unsigned (*)(int))par_u8;
    int (*ps)(int) = (int (*)(int))par_s8;
    unsigned (*pu16)(int) = (unsigned (*)(int))par_u16;
    int (*ps16)(int) = (int (*)(int))par_s16;
    unsigned crc = 0;
    int i;

    CHK(wrap_u8(5), (unsigned char)(250 + 15));
    CHK(wrap_u8(n_global), (unsigned char)(250 + 3 * 300));
    CHK(wrap_s8(3), (signed char)(120 + 15));
    CHK(wrap_s8(n_global), (signed char)(unsigned char)(120 + 5 * 300));
    CHK(wrap_u16(1), (unsigned short)(65530u * 3 + 7));
    CHK(wrap_u16(4), 0xff32);
    CHK(wrap_s16(1), (short)(33000 - 65536));
    CHK(wrap_s16(n_global), (short)(unsigned short)(32000 + 1000 * 300));

    CHK(pu(0x1234ff), 0xff);              /* d = 0 after ++, + 255 */
    CHK(pu(0x7f), 0x80 + 0x7f);
    CHK(ps(0x1281), -127 * 2);
    CHK(ps(0x7f), 254);
    CHK(pu16(0x5ffff), 0x10000);
    CHK(ps16(0x18000), -32769);
    CHK(par_u8(200), 201 + 200);

    CHK(mixed(0x1ff80), (signed char)0x80 + (short)0xff80 + 0x80 + 0xff80);
    CHK(mixed(-1), -1 + -1 + 255 + 65535);

    CHK(ranges(0x1f3, 0x1ff),
        0xf3 + (0 << 8) + (1 << 16) + ((unsigned short)(255 * 255 * 2) << 1)
            + ((unsigned)(signed char)0xff << 20) + (0xf3 ^ 1));
    CHK(ranges(-1, 0x80),
        0xff + ((unsigned char)(0xffffffffu >> 20) << 8) + (0 << 16)
            + ((unsigned short)(128 * 128 * 2) << 1)
            + ((unsigned)(signed char)0x80 << 20) + (0xff ^ 0));

    CHK(escaped(), 0xA5 + 9);

    for (i = 0; i < 256; i++)
        crc = crcu8((unsigned char)i, (unsigned short)crc);
    CHK(crc, 0xbad3);
    CHK(crcu8(0x5a, 0xffff), 0x7b3f);

    CHK(lowbyte(0xfedc, 40), 0x628ab2a4);

    CHK(shifts(0x1f2f3, 0xfe, (short)0x8003), 0x80c9cd);
    CHK(shifts(-1, 7, 100), 0x5011d);

    CHK(keep_across(0x181), 2 * (signed char)0x81 + (signed char)0x81
        + (unsigned short)(0x181 * 7));

    us(bad ? "\nNARROWLOCALFAIL\n" : "NARROWLOCALOK\n");
}
