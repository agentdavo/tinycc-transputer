/* Runtime regression for conservative __builtin_object_size semantics.
 * Expected: SBOSOK\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static int calls;
static char object[8];

static void *get_object(void)
{
    calls++;
    return object;
}

static void uart_putc(int c)
{
    UART_DATA = (unsigned long)c;
}

void test_main(void)
{
    unsigned long unknown_max;
    unsigned long unknown_zero;
    int ok;

    unknown_max = __builtin_object_size(get_object(), 0);
    unknown_zero = __builtin_object_size(get_object(), 2);
    ok = unknown_max == ~0UL && unknown_zero == 0 && calls == 0;

    if (ok) {
        uart_putc('B'); uart_putc('O'); uart_putc('S');
        uart_putc('O'); uart_putc('K');
    } else {
        uart_putc('F'); uart_putc('A'); uart_putc('I'); uart_putc('L');
    }
    uart_putc('\n');
}
