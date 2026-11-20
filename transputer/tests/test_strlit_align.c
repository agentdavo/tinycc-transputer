/*
 * test_strlit_align.c - string literals of four or more bytes (including
 * the NUL) start on a word boundary, so word-at-a-time string routines take
 * their aligned path.  Before the change TinyCC packed literals byte by
 * byte: Dhrystone's "DHRYSTONE PROGRAM, 2'ND STRING" sat 3 bytes off a word.
 * Odd lengths in a row would push the next literal off alignment.  Prints A
 * per aligned literal, X otherwise, then checks the bytes survived.
 *
 * Expected: SAAAAAAOK\nH
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static void check(const char *p)
{
    uart_putc(((unsigned)p & 3u) == 0u ? 'A' : 'X');
}

static const char *table[] = { "pqrst", "uvw" };

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

void test_main(void)
{
    const char *s1 = "abcde";      /* 6 bytes */
    const char *s2 = "fgh";        /* 4 bytes: still aligned */
    const char *s3 = "ijklmnopq";  /* 10 bytes */
    const char *s4 = "DHRYSTONE PROGRAM, 2'ND STRING";
    check(s1);
    check(s2);
    check(s3);
    check(s4);
    check(table[0]);
    check(table[1]);
    if (same(s1, "abcde") && same(s2, "fgh") && same(s3, "ijklmnopq")
        && s4[30] == 0 && s4[29] == 'G' && same(table[1], "uvw")) {
        uart_putc('O');
        uart_putc('K');
    } else {
        uart_putc('F');
    }
    uart_putc('\n');
}
