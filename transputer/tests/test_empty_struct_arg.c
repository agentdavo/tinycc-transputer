/*
 * Empty structs occupy no argument words in the GCC ABI.  Modern Linux uses
 * this for configuration-dependent token arguments such as kmalloc_token_t.
 *
 * Expected: SE123OK\nH
 */

struct empty { };

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static int empty_middle(int a, struct empty token, int b)
{
    return a * 100 + b;
}

static int empty_first(struct empty token, int value)
{
    return value + 7;
}

static int empty_last(int value, struct empty token)
{
    return value + 9;
}

static int wrapper(int size, int flags, struct empty token)
{
    return empty_middle(size, token, flags);
}

void test_main(void)
{
    struct empty token = { };
    int ok1;
    int ok2;
    int ok3;

    ok1 = wrapper(24, 11, token) == 2411;
    ok2 = empty_first(token, 5) == 12;
    ok3 = empty_last(8, token) == 17;

    uart_putc('E');
    uart_putc(ok1 ? '1' : 'x');
    uart_putc(ok2 ? '2' : 'x');
    uart_putc(ok3 ? '3' : 'x');
    uart_putc(ok1 && ok2 && ok3 ? 'O' : 'F');
    uart_putc('K');
    uart_putc('\n');
}
