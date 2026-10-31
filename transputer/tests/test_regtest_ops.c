/* Exercise pop and the register test instructions (testlds, teststs,
 * testhardchan) in the t4 emulator.  Probes live in test_regtest_ops_fn.S
 * (raw .byte encodings); this driver checks their observable results.
 * Run via test_runner_with_uart.sh. */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

#define ERROR_FLAG 0x80000000u

extern void rt_pop(unsigned int *io);
extern void rt_status(unsigned int *io);
extern void rt_hardchan(unsigned int *io);

static unsigned int io[8];
static int pass = 1;

static void check(const char *name, int ok)
{
    uart_puts(name);
    uart_puts(ok ? ":P " : ":F ");
    if (!ok) pass = 0;
}

void test_main(void)
{
    int i;

    /* pop: A=3 B=2 C=1 -> A=2 B=1 */
    for (i = 0; i < 8; i++) io[i] = 0;
    rt_pop(io);
    check("pop", io[0] == 2 && io[1] == 1);

    /* teststs(S | Error) must make testerr report the error, and testerr
     * must then leave StatusReg as it was on entry with Error clear. */
    for (i = 0; i < 8; i++) io[i] = 0xFFFFFFFFu;
    rt_status(io);
    check("ldsts", (io[0] & ERROR_FLAG) == 0 && io[1] == 0 &&
                   (io[2] & ERROR_FLAG) == 0 && io[2] == io[0]);

    /* testhardchan: values leave in push order after three shifts, the
     * channel's Count register starts idle, and BReg := CReg. */
    for (i = 0; i < 8; i++) io[i] = 0xFFFFFFFFu;
    rt_hardchan(io);
    check("hchan", io[2] == 0 &&
                   io[3] == 0x11 && io[4] == 0x22 && io[5] == 0x33 &&
                   io[6] == 0x55);

    uart_puts(pass ? "\nALL-PASS\n" : "\nFAILED\n");
}
