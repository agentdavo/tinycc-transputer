/*
 * test_uclibc_malloc.c — Test uClibc-ng malloc/free via real libc.a
 *
 * This exercises the actual malloc path that BusyBox hush uses.
 * Must be linked with uClibc-ng libc.a and syscall_stub.c.
 *
 * UART oracle: UCLIBC_MALLOC:PASS
 */
extern void *malloc(unsigned long size);
extern void free(void *ptr);

#define UART (*(volatile unsigned int *)0x10000000u)

static void uart_puts(const char *s) { while (*s) UART = *s++; }
static void uart_hex32(unsigned v) {
    static const char h[] = "0123456789ABCDEF";
    int i;
    for (i = 28; i >= 0; i -= 4) UART = h[(v >> i) & 0xF];
}

static int test_malloc_free(unsigned size, unsigned char pattern) {
    unsigned char *p;
    unsigned i;

    p = (unsigned char *)malloc(size);
    if (!p) {
        uart_puts("NULL");
        return 0;
    }
    uart_hex32((unsigned)(unsigned long)p);

    /* Write pattern */
    for (i = 0; i < size; i++)
        p[i] = (unsigned char)(pattern + (i & 0xFF));

    /* Read back */
    for (i = 0; i < size; i++) {
        if (p[i] != (unsigned char)(pattern + (i & 0xFF))) {
            uart_puts(":CORRUPT");
            free(p);
            return 0;
        }
    }

    free(p);
    uart_puts(":OK");
    return 1;
}

/* Mimic exact hush pattern: alloc, free, alloc, free, alloc */
static int hush_pattern_test(void) {
    void *p;
    int ok = 1;

    uart_puts("H64:");
    p = malloc(64);
    uart_hex32((unsigned)(unsigned long)p);
    if (p) { free(p); uart_puts(":OK\n"); }
    else { uart_puts(":NULL\n"); ok = 0; }

    uart_puts("H128:");
    p = malloc(128);
    uart_hex32((unsigned)(unsigned long)p);
    if (p) { free(p); uart_puts(":OK\n"); }
    else { uart_puts(":NULL\n"); ok = 0; }

    uart_puts("H256:");
    p = malloc(256);
    uart_hex32((unsigned)(unsigned long)p);
    if (p) { free(p); uart_puts(":OK\n"); }
    else { uart_puts(":NULL\n"); ok = 0; }

    uart_puts("H1336:");
    p = malloc(1336);
    uart_hex32((unsigned)(unsigned long)p);
    if (p) { free(p); uart_puts(":OK\n"); }
    else { uart_puts(":NULL\n"); ok = 0; }

    return ok;
}

int main(void) {
    int pass = 1;

    uart_puts("M32:");
    if (!test_malloc_free(32, 0xAA)) { uart_puts("\n"); pass = 0; } else uart_puts("\n");

    uart_puts("M64:");
    if (!test_malloc_free(64, 0xBB)) { uart_puts("\n"); pass = 0; } else uart_puts("\n");

    uart_puts("M128:");
    if (!test_malloc_free(128, 0xCC)) { uart_puts("\n"); pass = 0; } else uart_puts("\n");

    uart_puts("M256:");
    if (!test_malloc_free(256, 0xDD)) { uart_puts("\n"); pass = 0; } else uart_puts("\n");

    uart_puts("M1024:");
    if (!test_malloc_free(1024, 0xEE)) { uart_puts("\n"); pass = 0; } else uart_puts("\n");

    uart_puts("M4096:");
    if (!test_malloc_free(4096, 0xFF)) { uart_puts("\n"); pass = 0; } else uart_puts("\n");

    /* Test the exact hush malloc/free pattern */
    uart_puts("HUSH_PAT:");
    if (hush_pattern_test()) uart_puts("OK\n");
    else { uart_puts("FAIL\n"); pass = 0; }

    if (pass)
        uart_puts("UCLIBC_MALLOC:PASS\n");
    else
        uart_puts("UCLIBC_MALLOC:FAIL\n");

    return 0;
}
