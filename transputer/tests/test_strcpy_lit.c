/*
 * test_strcpy_lit.c - strcpy(dst, "literal") is lowered to one MOVE of the
 * literal's bytes and its NUL.  Checks the copy and the returned pointer for
 * local, global and pointer destinations, an offset into a literal, the empty
 * string, a const array source, and that a writable array source (whose
 * bytes change at run time) still copies what it holds at the call.  The
 * strcpy here is the program's own, so a call that is not lowered still
 * works.  One letter per check: upper case passes, '!' fails.
 *
 * Expected: SABCDEFGHI\nH
 */

#include <stddef.h>

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static void check(int ok, int letter)
{
    uart_putc(ok ? letter : '!');
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

char gbuf[40];
static const char carr[] = "const array";
char warr[8] = "abc";

void test_main(void)
{
    char loc[40];
    char *p = gbuf + 3, *r;
    int i;

    for (i = 0; i < 40; i++) { loc[i] = 'x'; gbuf[i] = 'y'; }

    r = strcpy(loc, "DHRYSTONE PROGRAM, 1'ST STRING");
    check(r == loc && same(loc, "DHRYSTONE PROGRAM, 1'ST STRING") &&
          loc[31] == 'x', 'A');
    strcpy(gbuf, "abc");
    check(same(gbuf, "abc") && gbuf[4] == 'y', 'B');
    r = strcpy(p, "hello" + 2);
    check(r == p && same(gbuf + 3, "llo") && gbuf[7] == 'y', 'C');
    r = strcpy(loc + 1, "");
    check(r == loc + 1 && loc[1] == 0 && loc[2] == 'R', 'D');
    strcpy(loc, carr);
    check(same(loc, "const array"), 'E');
    warr[1] = 'Z';
    strcpy(loc, warr);
    check(same(loc, "aZc"), 'F');
    p = warr;
    strcpy(loc, p);
    check(same(loc, "aZc"), 'G');
    /* odd lengths back to back, unaligned destination */
    strcpy(loc + 1, "1234567");
    strcpy(loc + 9, "89");
    check(same(loc + 1, "1234567") && same(loc + 9, "89"), 'H');
    check(strcpy(loc, "q")[0] == 'q' && loc[1] == 0, 'I');
    uart_putc('\n');
}
