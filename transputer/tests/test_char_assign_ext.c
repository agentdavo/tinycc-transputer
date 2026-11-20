/*
 * test_char_assign_ext.c - the value of a char or short assignment.  When
 * the right side is a char (short) lvalue of the destination's own type and
 * signedness the compiler skips the re-extension after the store, because
 * the load already extended it.  Every other source must still be truncated
 * and extended to the destination type.  Prints one letter per check:
 * upper case passes, '!' fails.
 *
 * Expected: SABCDEFGHIJKLM\nH
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static void check(int ok, int letter)
{
    uart_putc(ok ? letter : '!');
}

struct bits { int lo : 4; signed char sc : 3; unsigned int u : 5; };

static volatile int vneg = -5, vbig = 0x1ff, vhalf = 0x18000;
static signed char gsc[4] = { -1, -128, 127, 0 };
static unsigned char guc[4] = { 0xff, 0x80, 0x7f, 0 };
static short gss[2] = { -2, -32768 };
static unsigned short gus[2] = { 0xfffe, 0x8000 };

/* strcpy-shaped: the loop must stop at the NUL and copy the high bytes */
static int copy_count(char *d, const char *s)
{
    int n = 0;
    while ((*d++ = *s++))
        n++;
    return n;
}

void test_main(void)
{
    signed char sc, sc2;
    unsigned char uc, uc2;
    short ss;
    unsigned short us;
    int r;
    char buf[8];
    struct bits b;

    /* same type and signedness: value is the extended byte */
    sc2 = gsc[1];
    r = (sc = sc2);
    check(r == -128 && sc == -128, 'A');
    uc2 = guc[0];
    r = (uc = uc2);
    check(r == 255, 'B');
    r = (sc = gsc[0]) + 1;
    check(r == 0, 'C');

    /* signedness differs: must be re-extended */
    r = (uc = gsc[0]);
    check(r == 255, 'D');
    r = (sc = guc[1]);
    check(r == -128, 'E');

    /* int source: truncated and extended */
    r = (sc = vbig);
    check(r == -1, 'F');
    r = (uc = vneg);
    check(r == 251, 'G');

    /* shorts */
    r = (ss = gss[1]);
    check(r == -32768, 'H');
    r = (us = gss[0]);
    check(r == 0xfffe, 'I');
    r = (ss = vhalf);
    check(r == -32768, 'J');
    r = (ss = gus[1]);
    check(r == -32768, 'K');

    /* bitfield source */
    b.lo = 0; b.u = 0; b.sc = -3;
    r = (sc = b.sc);
    check(r == -3, 'L');

    /* the strcpy loop, with a high-bit byte in the string */
    {
        static const char src[] = { 'a', (char)0xe9, 'b', 0 };
        int n = copy_count(buf, src);
        check(n == 3 && buf[1] == src[1] && buf[3] == 0 && buf[2] == 'b', 'M');
    }
    uart_putc('\n');
}
