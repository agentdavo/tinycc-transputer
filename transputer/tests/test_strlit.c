/*
 * test_strlit.c — Minimal reproducer for bFLT string literal relocation bug.
 *
 * When compiled and run as a standalone ELF on T4, string literals work.
 * When converted to bFLT and run under the kernel, string literal addresses
 * passed to functions are garbage.
 *
 * This test verifies string literal access patterns to isolate the bug.
 */

static void uart_putc(int c) { *(volatile int *)0x10000000 = c; }

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void uart_hex8(unsigned int v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int n = (v >> i) & 0xf;
        uart_putc(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

/* Test 1: String literal as direct arg to uart_puts (known to work) */
static void test_direct(void)
{
    uart_puts("T1:");
    uart_puts("HELLO");
    uart_puts("\r\n");
}

/* Test 2: String literal address stored in local, then passed */
static void test_local_ptr(void)
{
    const char *s = "WORLD";
    uart_puts("T2:");
    uart_puts(s);
    uart_puts("\r\n");
}

/* Test 3: String literal passed to a comparison function */
static int my_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

/* Test 3a: Show raw addresses and first bytes */
static void test_strcmp_debug(void)
{
    const char *input = "echo";
    const char *lit = "echo";
    uart_puts("T3a:input=");
    uart_hex8((unsigned int)input);
    uart_puts(" lit=");
    uart_hex8((unsigned int)lit);
    uart_puts(" in[0]=");
    uart_putc(input[0]);
    uart_puts(" lit[0]=");
    uart_putc(lit[0]);
    uart_puts("\r\n");
}

/* Test 3b: Pass string literal as 1st arg (like uart_puts) */
static void show_ptr(const char *s)
{
    uart_puts("T3b:ptr=");
    uart_hex8((unsigned int)s);
    uart_puts(" [0]=");
    if (s && (unsigned int)s > 0x80000000u)
        uart_putc(s[0]);
    else
        uart_puts("BAD");
    uart_puts("\r\n");
}

/* Test 3c: Pass string literal as 2nd arg */
static void show_ptr2(int dummy, const char *s)
{
    uart_puts("T3c:ptr=");
    uart_hex8((unsigned int)s);
    uart_puts(" [0]=");
    if (s && (unsigned int)s > 0x80000000u)
        uart_putc(s[0]);
    else
        uart_puts("BAD");
    uart_puts("\r\n");
}

static void test_strcmp(void)
{
    const char *input = "echo";
    const char *literal = "echo";
    int result;

    test_strcmp_debug();
    show_ptr("echo");
    show_ptr2(0, "echo");

    /* Test the comparison result explicitly */
    result = my_strcmp(input, literal);
    uart_puts("T3:result=");
    if (result == 0) uart_putc('0');
    else if (result > 0) uart_putc('+');
    else uart_putc('-');
    uart_puts(" val=");
    uart_hex8((unsigned int)result);
    uart_puts("\r\n");

    /* Also test with inline comparison */
    uart_puts("T3i:");
    {
        const char *a = input;
        const char *b = literal;
        int match = 1;
        while (*a && *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (*a != *b) match = 0;
        if (match)
            uart_puts("MATCH");
        else
            uart_puts("FAIL");
    }
    uart_puts("\r\n");
}

/* Test 4: Print addresses of string literals to check relocation */
static void test_addrs(void)
{
    const char *s1 = "AAA";
    const char *s2 = "BBB";

    uart_puts("T4:s1=");
    uart_hex8((unsigned int)s1);
    uart_puts(" s2=");
    uart_hex8((unsigned int)s2);
    uart_puts(" s1[0]=");
    uart_putc(s1[0]);
    uart_puts(" s2[0]=");
    uart_putc(s2[0]);
    uart_puts("\r\n");
}

/* Test 5: Static char array (data section, not rodata) */
static char static_str[] = { 'S','T','A','T','I','C',0 };

static void test_static_arr(void)
{
    uart_puts("T5:");
    uart_puts(static_str);
    uart_puts("\r\n");
}

/* Test 6: String literal used in two different function calls */
static int my_strlen(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

static void test_strlen_and_puts(void)
{
    const char *s = "FOOBAR";
    int len = my_strlen(s);
    uart_puts("T6:len=");
    uart_putc('0' + len);
    uart_puts(" str=");
    uart_puts(s);
    uart_puts("\r\n");
}

int test_main(void)
{
    uart_puts("=== strlit test ===\r\n");
    test_direct();
    test_local_ptr();
    test_strcmp();
    test_addrs();
    test_static_arr();
    test_strlen_and_puts();
    uart_puts("=== done ===\r\n");
    return 0;
}
