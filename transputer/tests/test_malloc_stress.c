/*
 * test_malloc_stress.c — Test malloc/free patterns that fail in BusyBox hush.
 *
 * Hush shows: malloc(64)=OK, malloc(128)=OK, malloc(256)=FAIL
 * This test reproduces the exact pattern to isolate the bug.
 *
 * UART oracle: MALLOC_STRESS:PASS
 */
#define UART (*(volatile unsigned int *)0x10000000u)

static void uart_puts(const char *s) { while (*s) UART = *s++; }
static void uart_hex32(unsigned v) {
    static const char h[] = "0123456789ABCDEF";
    int i;
    for (i = 28; i >= 0; i -= 4)
        UART = h[(v >> i) & 0xF];
}

extern long __transputer_syscall(long number, long a1, long a2, long a3,
                                 long a4, long a5, long a6);
#define __NR_brk  214
#define __NR_exit_group 94

/*
 * Minimal bump allocator using brk syscall.
 * No free support — just tests that brk extends correctly.
 */
static unsigned long heap_start;
static unsigned long heap_end;

static void heap_init(void) {
    heap_start = (unsigned long)__transputer_syscall(__NR_brk, 0, 0, 0, 0, 0, 0);
    heap_end = heap_start;
}

static void *simple_malloc(unsigned long size) {
    unsigned long new_end;
    unsigned long result;

    /* Align to 8 bytes */
    size = (size + 7) & ~7UL;
    new_end = heap_end + size;

    /* Extend brk */
    result = (unsigned long)__transputer_syscall(__NR_brk, (long)new_end, 0, 0, 0, 0, 0);
    if (result < new_end)
        return (void *)0; /* brk failed */

    result = heap_end;
    heap_end = new_end;
    return (void *)result;
}

/* Test: write a pattern, read it back */
static int test_region(void *ptr, unsigned long size, unsigned char pattern) {
    unsigned char *p = (unsigned char *)ptr;
    unsigned long i;

    /* Write pattern */
    for (i = 0; i < size; i++)
        p[i] = (unsigned char)(pattern + (i & 0xFF));

    /* Read back and verify */
    for (i = 0; i < size; i++) {
        if (p[i] != (unsigned char)(pattern + (i & 0xFF)))
            return 0; /* FAIL */
    }
    return 1; /* PASS */
}

void test_main(void) {
    int pass = 1;
    void *p;

    heap_init();
    uart_puts("BRK:");
    uart_hex32(heap_start);
    uart_puts("\n");

    /* Test 1: 64 bytes */
    uart_puts("M64:");
    p = simple_malloc(64);
    if (p) {
        uart_hex32((unsigned long)p);
        if (test_region(p, 64, 0xAA))
            uart_puts(":OK\n");
        else
            { uart_puts(":CORRUPT\n"); pass = 0; }
    } else {
        uart_puts("NULL\n"); pass = 0;
    }

    /* Test 2: 128 bytes */
    uart_puts("M128:");
    p = simple_malloc(128);
    if (p) {
        uart_hex32((unsigned long)p);
        if (test_region(p, 128, 0xBB))
            uart_puts(":OK\n");
        else
            { uart_puts(":CORRUPT\n"); pass = 0; }
    } else {
        uart_puts("NULL\n"); pass = 0;
    }

    /* Test 3: 256 bytes */
    uart_puts("M256:");
    p = simple_malloc(256);
    if (p) {
        uart_hex32((unsigned long)p);
        if (test_region(p, 256, 0xCC))
            uart_puts(":OK\n");
        else
            { uart_puts(":CORRUPT\n"); pass = 0; }
    } else {
        uart_puts("NULL\n"); pass = 0;
    }

    /* Test 4: 1336 bytes (INIT_G size) */
    uart_puts("M1336:");
    p = simple_malloc(1336);
    if (p) {
        uart_hex32((unsigned long)p);
        if (test_region(p, 1336, 0xDD))
            uart_puts(":OK\n");
        else
            { uart_puts(":CORRUPT\n"); pass = 0; }
    } else {
        uart_puts("NULL\n"); pass = 0;
    }

    /* Test 5: 4096 bytes */
    uart_puts("M4096:");
    p = simple_malloc(4096);
    if (p) {
        uart_hex32((unsigned long)p);
        if (test_region(p, 4096, 0xEE))
            uart_puts(":OK\n");
        else
            { uart_puts(":CORRUPT\n"); pass = 0; }
    } else {
        uart_puts("NULL\n"); pass = 0;
    }

    if (pass)
        uart_puts("MALLOC_STRESS:PASS\n");
    else
        uart_puts("MALLOC_STRESS:FAIL\n");
}
