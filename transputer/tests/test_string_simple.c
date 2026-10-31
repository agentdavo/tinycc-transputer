/*
 * Simpler string function test
 * Puts bootstrap code first using section attributes
 */

/* These get placed in .text.start section which comes first */
__attribute__((section(".text.start")))
void _start(void);

/* UART helpers */
#define UART_ADDR  ((volatile unsigned int*)0x10000000)

static void uart_putc(char c) {
    *UART_ADDR = (unsigned char)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

/* Simple strlen */
static unsigned int my_strlen(const char *s)
{
    const char *start = s;
    while (*s)
        s++;
    return s - start;
}

/* Test string */
static const char *test_str = "Hello World";
static char buffer[64];

/* Simple strcpy */
static char *my_strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

/* Main test function */
static void run_tests(void)
{
    unsigned int len;
    int i;

    uart_puts("Test Starting\n");

    /* Test strlen */
    uart_putc('1');
    len = my_strlen(test_str);
    if (len == 11) {
        uart_putc('A');  /* strlen OK */
    } else {
        uart_putc('F');  /* strlen FAIL */
    }

    /* Test strcpy */
    uart_putc('2');
    for (i = 0; i < 64; i++)
        buffer[i] = 0;
    my_strcpy(buffer, test_str);
    if (buffer[0] == 'H' && buffer[10] == 'd') {
        uart_putc('B');  /* strcpy OK */
    } else {
        uart_putc('G');  /* strcpy FAIL */
    }

    uart_puts("\nDONE\n");
}

/* Entry point - must be in .text.start section */
__attribute__((section(".text.start")))
void _start(void)
{
    /* Inline assembly to adjust workspace */
    /* We need WPtr to be above the code, not at the code address */

    /* The first thing we do is call our test function */
    /* TCC's prolog will handle workspace setup */
    run_tests();

    /* Infinite loop */
    while (1);
}
