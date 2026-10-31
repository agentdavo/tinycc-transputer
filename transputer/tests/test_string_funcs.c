/*
 * Test file for string functions that fail in kernel
 * Uses inline bootstrap setup
 */

#define UART_ADDR  ((volatile unsigned int*)0x10000000)
#define DEBUG_ADDR ((volatile unsigned int*)0x80001000)

typedef unsigned int size_t;

/* Simple UART output - writes directly to MMIO */
static void uart_putc(char c) {
    *UART_ADDR = (unsigned char)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

/* Print hex digit */
static void uart_puthex_nibble(unsigned int n) {
    n &= 0xF;
    if (n < 10)
        uart_putc('0' + n);
    else
        uart_putc('A' + n - 10);
}

static void uart_puthex(unsigned int n) {
    int i;
    for (i = 7; i >= 0; i--) {
        uart_puthex_nibble(n >> (i * 4));
    }
}

/* Simple strlen implementation */
static size_t my_strlen(const char *s)
{
    const char *start = s;
    while (*s)
        s++;
    return s - start;
}

/* Simple strcpy implementation */
static char *my_strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

/* Test global string */
static char buffer[64];
static const char *test_str = "Hello World";

/* Test strlen with global string */
static int test_strlen_global(void)
{
    size_t len;
    uart_puts("test_strlen_global: ");
    len = my_strlen(test_str);
    uart_puts("len=");
    uart_puthex(len);
    uart_putc('\n');
    return len == 11 ? 0 : -1;
}

/* Test strlen with local string */
static int test_strlen_local(void)
{
    const char *s = "Test";
    size_t len;
    uart_puts("test_strlen_local: ");
    len = my_strlen(s);
    uart_puts("len=");
    uart_puthex(len);
    uart_putc('\n');
    return len == 4 ? 0 : -1;
}

/* Test strcpy */
static int test_strcpy(void)
{
    int i;
    uart_puts("test_strcpy: ");

    /* Clear buffer first */
    for (i = 0; i < 64; i++)
        buffer[i] = 0;

    my_strcpy(buffer, test_str);

    uart_puts("buf[0]=");
    uart_putc(buffer[0]);
    uart_puts(" buf[10]=");
    uart_putc(buffer[10]);
    uart_putc('\n');

    /* Check first char */
    if (buffer[0] != 'H')
        return -1;
    /* Check last char */
    if (buffer[10] != 'd')
        return -2;
    return 0;
}

/* Simulate memory pool allocation like memblock_alloc */
static char memory_pool[4096] __attribute__((aligned(8)));  /* allocations align offsets only */
static size_t pool_used = 0;

static void *simple_alloc(size_t size, size_t align)
{
    size_t aligned_used;
    void *ptr;

    uart_puts("simple_alloc: size=");
    uart_puthex(size);
    uart_puts(" align=");
    uart_puthex(align);
    uart_puts(" pool_used=");
    uart_puthex(pool_used);

    /* Align the current position */
    if (align > 1) {
        aligned_used = (pool_used + align - 1) & ~(align - 1);
    } else {
        aligned_used = pool_used;
    }

    uart_puts(" aligned=");
    uart_puthex(aligned_used);

    /* Check if we have enough space */
    if (aligned_used + size > sizeof(memory_pool)) {
        uart_puts(" FAIL:OOM\n");
        return (void *)0;
    }

    ptr = &memory_pool[aligned_used];
    pool_used = aligned_used + size;

    uart_puts(" ptr=");
    uart_puthex((unsigned int)(long)ptr);
    uart_puts(" OK\n");
    return ptr;
}

/* Test allocation */
static int test_alloc(void)
{
    void *p1, *p2;

    uart_puts("test_alloc:\n");
    pool_used = 0;  /* Reset pool */

    p1 = simple_alloc(16, 4);
    if (!p1)
        return -1;

    p2 = simple_alloc(32, 8);
    if (!p2)
        return -2;

    /* Check p2 is after p1 */
    if ((char *)p2 <= (char *)p1) {
        uart_puts("  FAIL: p2 <= p1\n");
        return -3;
    }

    /* Check alignment */
    if (((size_t)(long)p2 & 7) != 0) {
        uart_puts("  FAIL: p2 not aligned\n");
        return -4;
    }

    uart_puts("  test_alloc OK\n");
    return 0;
}

/* Combined test like setup_command_line does */
static int test_command_line_pattern(void)
{
    const char *boot_command_line = "console=ttyS0";
    char *saved_command_line;
    char *static_command_line;
    size_t len;

    uart_puts("test_command_line_pattern:\n");
    pool_used = 0;  /* Reset pool */

    /* Calculate length */
    len = my_strlen(boot_command_line) + 1;
    uart_puts("  len=");
    uart_puthex(len);
    uart_putc('\n');

    /* Allocate saved_command_line */
    saved_command_line = simple_alloc(len, 8);
    if (!saved_command_line) {
        uart_puts("  FAIL: saved_command_line alloc\n");
        return -1;
    }

    /* Allocate static_command_line */
    static_command_line = simple_alloc(len, 8);
    if (!static_command_line) {
        uart_puts("  FAIL: static_command_line alloc\n");
        return -2;
    }

    /* Copy strings */
    my_strcpy(saved_command_line, boot_command_line);
    my_strcpy(static_command_line, boot_command_line);

    uart_puts("  saved[0]=");
    uart_putc(saved_command_line[0]);
    uart_puts(" static[0]=");
    uart_putc(static_command_line[0]);
    uart_putc('\n');

    /* Verify copies */
    if (saved_command_line[0] != 'c') {
        uart_puts("  FAIL: saved[0] != 'c'\n");
        return -3;
    }
    if (static_command_line[0] != 'c') {
        uart_puts("  FAIL: static[0] != 'c'\n");
        return -4;
    }

    uart_puts("  test_command_line_pattern OK\n");
    return 0;
}

/* Do all tests - entry point called from bootstrap */
void test_main(void)
{
    int result;

    uart_puts("String Functions Test Starting...\n");

    uart_puts("\n=== TEST 1: strlen global ===\n");
    result = test_strlen_global();
    if (result != 0) {
        uart_puts("FAIL: test_strlen_global returned ");
        uart_puthex(result);
        uart_putc('\n');
        goto fail;
    }
    uart_puts("PASS\n");

    uart_puts("\n=== TEST 2: strlen local ===\n");
    result = test_strlen_local();
    if (result != 0) {
        uart_puts("FAIL: test_strlen_local returned ");
        uart_puthex(result);
        uart_putc('\n');
        goto fail;
    }
    uart_puts("PASS\n");

    uart_puts("\n=== TEST 3: strcpy ===\n");
    result = test_strcpy();
    if (result != 0) {
        uart_puts("FAIL: test_strcpy returned ");
        uart_puthex(result);
        uart_putc('\n');
        goto fail;
    }
    uart_puts("PASS\n");

    uart_puts("\n=== TEST 4: alloc ===\n");
    result = test_alloc();
    if (result != 0) {
        uart_puts("FAIL: test_alloc returned ");
        uart_puthex(result);
        uart_putc('\n');
        goto fail;
    }
    uart_puts("PASS\n");

    uart_puts("\n=== TEST 5: command line pattern ===\n");
    result = test_command_line_pattern();
    if (result != 0) {
        uart_puts("FAIL: test_command_line_pattern returned ");
        uart_puthex(result);
        uart_putc('\n');
        goto fail;
    }
    uart_puts("PASS\n");

    uart_puts("\n=== ALL TESTS PASSED ===\n");
    *DEBUG_ADDR = 0xDEADBEEF;
    while (1);

fail:
    uart_puts("\n=== TESTS FAILED ===\n");
    *DEBUG_ADDR = 0xDEAD0000 | (result & 0xFFFF);
    while (1);
}
