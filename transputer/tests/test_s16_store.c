/* Test: 16-bit (short) stores through pointers - SLOB allocator pattern
 *
 * The SLOB allocator stores slobidx_t (s16) values via:
 *   s[0].units = size;    // 16-bit store at s+0
 *   s[1].units = offset;  // 16-bit store at s+2
 *
 * If TCC writes 4 bytes instead of 2 for these stores,
 * it corrupts adjacent memory in the heap - causing the
 * ctor field corruption we see in the kernel.
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

static void uart_puthex8(unsigned int v) {
    int nib;
    nib = (v >> 4) & 0xF;
    uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    nib = v & 0xF;
    uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
}

static void uart_puthex32(unsigned int v) {
    uart_puthex8((v >> 24) & 0xFF);
    uart_puthex8((v >> 16) & 0xFF);
    uart_puthex8((v >> 8) & 0xFF);
    uart_puthex8(v & 0xFF);
}

typedef short slobidx_t;  /* s16, same as kernel */

struct slob_block {
    slobidx_t units;
};
typedef struct slob_block slob_t;

/* Test 1: Basic 16-bit store through pointer - does it corrupt adjacent memory? */
void test_main(void)
{
    unsigned char buf[32];
    slob_t *s;
    int i;
    int pass = 1;

    /* Fill buffer with known pattern */
    for (i = 0; i < 32; i++)
        buf[i] = 0xAA;

    uart_puts("T1:");

    /* Store 16-bit value at offset 0 */
    s = (slob_t *)&buf[4];
    s[0].units = 42;  /* should write 2 bytes at buf[4..5] */

    /* Check buf[6] and buf[7] are still 0xAA (not corrupted) */
    uart_puts("b6=");
    uart_puthex8(buf[6]);
    uart_puts(",b7=");
    uart_puthex8(buf[7]);
    if (buf[6] != 0xAA || buf[7] != 0xAA) {
        uart_puts(":FAIL_CORRUPT");
        pass = 0;
    } else {
        uart_puts(":ok");
    }

    /* Check written value */
    uart_puts(",val=");
    uart_puthex8(buf[4]);
    uart_puthex8(buf[5]);

    uart_puts("\nT2:");

    /* Fill buffer again */
    for (i = 0; i < 32; i++)
        buf[i] = 0xBB;

    /* Store via s[1].units (offset +2 bytes from s) */
    s = (slob_t *)&buf[4];
    s[1].units = 99;  /* should write 2 bytes at buf[6..7] */

    /* Check buf[8] and buf[9] are still 0xBB */
    uart_puts("b8=");
    uart_puthex8(buf[8]);
    uart_puts(",b9=");
    uart_puthex8(buf[9]);
    if (buf[8] != 0xBB || buf[9] != 0xBB) {
        uart_puts(":FAIL_CORRUPT");
        pass = 0;
    } else {
        uart_puts(":ok");
    }

    uart_puts("\nT3:");

    /* Test the exact SLOB set_slob pattern */
    for (i = 0; i < 32; i++)
        buf[i] = 0xCC;

    s = (slob_t *)&buf[8];
    /* This is what set_slob does when size > 1: */
    s[0].units = 10;   /* size at buf[8..9] */
    s[1].units = 2048; /* offset at buf[10..11] */

    /* Check buf[12..15] are still 0xCC */
    uart_puts("b12=");
    uart_puthex8(buf[12]);
    uart_puts(",b13=");
    uart_puthex8(buf[13]);
    uart_puts(",b14=");
    uart_puthex8(buf[14]);
    uart_puts(",b15=");
    uart_puthex8(buf[15]);
    if (buf[12] != 0xCC || buf[13] != 0xCC) {
        uart_puts(":FAIL_CORRUPT");
        pass = 0;
    } else {
        uart_puts(":ok");
    }

    /* Also verify the stored values */
    uart_puts("\nT4:s0=");
    uart_puthex8(buf[8]);
    uart_puthex8(buf[9]);
    uart_puts(",s1=");
    uart_puthex8(buf[10]);
    uart_puthex8(buf[11]);

    /* Test negative values (set_slob also stores -offset) */
    uart_puts("\nT5:");
    for (i = 0; i < 32; i++)
        buf[i] = 0xDD;

    s = (slob_t *)&buf[4];
    s[0].units = -100;  /* negative s16 */

    uart_puts("b6=");
    uart_puthex8(buf[6]);
    uart_puts(",b7=");
    uart_puthex8(buf[7]);
    if (buf[6] != 0xDD || buf[7] != 0xDD) {
        uart_puts(":FAIL_CORRUPT");
        pass = 0;
    } else {
        uart_puts(":ok");
    }

    /* Overall result */
    uart_puts("\n");
    if (pass)
        uart_puts("PASS\n");
    else
        uart_puts("FAIL\n");
}
