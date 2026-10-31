/*
 * Standalone BogoMips for Transputer T4/T800 emulator
 *
 * Based on the classic Linux BogoMips calibration.
 * Uses LDTIMER for timing, UART for output.
 *
 * Expected UART output: "BOGOMIPS:<value>\n" e.g. "BOGOMIPS:3.45\n"
 * UART oracle: BOGOMIPS:
 */

/* UART MMIO */
#define UART_DATA    (*(volatile unsigned int *)0x10000000)
#define UART_STATUS  (*(volatile unsigned int *)0x10000004)
#define UART_TX_RDY  0x01

static void uart_putc(char c)
{
    while (!(UART_STATUS & UART_TX_RDY))
        ;
    UART_DATA = (unsigned int)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void uart_put_dec(unsigned long val)
{
    char buf[12];
    int i = 0;
    if (val == 0) {
        uart_putc('0');
        return;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0)
        uart_putc(buf[--i]);
}

/* Read the T800 low-priority timer via LDTIMER instruction.
 * Defined in test_bogomips_timer.S — returns timer value in AReg.
 * Low-priority timer ticks every 64us (15,625 Hz). */
extern unsigned int bogomips_ldtimer(void);

/* Portable delay loop — compiler cannot optimize away */
static void delay(unsigned long loops)
{
    unsigned long i;
    for (i = loops; i > 0; --i)
        __asm__ volatile ("" ::: "memory");
}

void test_main(void)
{
    unsigned long loops_per_sec = 1;
    unsigned int t0, t1, ticks;

    uart_puts("Calibrating delay loop..");

    while ((loops_per_sec <<= 1)) {
        t0 = bogomips_ldtimer();
        delay(loops_per_sec);
        t1 = bogomips_ldtimer();
        ticks = t1 - t0;  /* ticks at 64us per tick (15625 Hz) */

        if (ticks >= 15625) {
            /* loops_per_sec iterations took >= 1 second (15625 ticks) */
            unsigned long lps;
            lps = (loops_per_sec / ticks) * 15625;

            uart_puts(" ok\nBOGOMIPS:");
            uart_put_dec(lps / 500000);
            uart_putc('.');
            /* Two decimal places */
            {
                unsigned long frac = (lps / 5000) % 100;
                if (frac < 10)
                    uart_putc('0');
                uart_put_dec(frac);
            }
            uart_putc('\n');
            return;
        }
    }
    uart_puts(" failed\n");
}
