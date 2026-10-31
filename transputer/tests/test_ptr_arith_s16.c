/* Test: Pointer arithmetic on 2-byte (s16/short) structs
 *
 * The SLOB allocator does: cur + units where cur is slob_t *
 * and sizeof(slob_t) = 2. If TCC computes ptr + N as
 * ptr + N*4 instead of ptr + N*2, the SLOB free list is corrupted.
 */

#define UART_DATA  (*(volatile unsigned int *)0x10000000)
#define UART_STAT  (*(volatile unsigned int *)0x10000004)

static void uart_putc(int c) {
    while (!(UART_STAT & 1)) {}
    UART_DATA = (unsigned int)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_puthex32(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int nib = (v >> i) & 0xF;
        uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    }
}

typedef short slobidx_t;

struct slob_block {
    slobidx_t units;
};
typedef struct slob_block slob_t;

void test_main(void)
{
    slob_t arr[16];
    slob_t *p;
    int i;
    unsigned int expected, actual;

    /* Fill with known pattern */
    for (i = 0; i < 16; i++)
        arr[i].units = (slobidx_t)(100 + i);

    uart_puts("sizeof_slob_t=");
    uart_puthex32(sizeof(slob_t));

    /* T1: Basic pointer + integer */
    uart_puts("\nT1:");
    p = &arr[0];
    expected = (unsigned int)p + 2 * 5;  /* 5 * sizeof(slob_t) = 10 bytes */
    actual = (unsigned int)(p + 5);
    uart_puts("exp=");
    uart_puthex32(expected);
    uart_puts(",act=");
    uart_puthex32(actual);
    if (expected == actual)
        uart_puts(":OK");
    else
        uart_puts(":FAIL");

    /* T2: Read via offset pointer */
    uart_puts("\nT2:");
    p = arr;
    uart_puts("p5=");
    uart_puthex32((unsigned int)(p + 5)->units);
    if ((p + 5)->units == 105)
        uart_puts(":OK");
    else
        uart_puts(":FAIL");

    /* T3: Pointer subtraction */
    uart_puts("\nT3:");
    {
        slob_t *a = &arr[2];
        slob_t *b = &arr[8];
        int diff = b - a;
        uart_puts("diff=");
        uart_puthex32(diff);
        if (diff == 6)
            uart_puts(":OK");
        else
            uart_puts(":FAIL");
    }

    /* T4: Array indexing same as pointer + offset */
    uart_puts("\nT4:");
    p = arr;
    actual = (unsigned int)&p[3];
    expected = (unsigned int)p + 3 * 2;
    uart_puts("exp=");
    uart_puthex32(expected);
    uart_puts(",act=");
    uart_puthex32(actual);
    if (expected == actual)
        uart_puts(":OK");
    else
        uart_puts(":FAIL");

    /* T5: SLOB pattern - cur + units where units is int */
    uart_puts("\nT5:");
    {
        slob_t *cur = &arr[2];
        int units = 4;
        slob_t *result = cur + units;
        expected = (unsigned int)cur + 4 * 2;  /* 4 * sizeof(slob_t) = 8 bytes */
        actual = (unsigned int)result;
        uart_puts("exp=");
        uart_puthex32(expected);
        uart_puts(",act=");
        uart_puthex32(actual);
        if (expected == actual)
            uart_puts(":OK");
        else
            uart_puts(":FAIL");
    }

    /* T6: set_slob pattern - write through computed pointer */
    uart_puts("\nT6:");
    {
        slob_t *cur = &arr[0];
        int units = 3;
        /* set_slob(cur + units, ...) */
        (cur + units)->units = 999;  /* should be at arr[3] */
        uart_puts("arr3=");
        uart_puthex32((unsigned int)arr[3].units);
        if (arr[3].units == 999)
            uart_puts(":OK");
        else
            uart_puts(":FAIL");
    }

    uart_putc('\n');
}
