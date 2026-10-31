/* Exercise the T9000-only instructions implemented in the t4 emulator.
 * Probes live in test_t9000_ops_fn.S (raw .byte encodings); this driver
 * checks their observable results.  Run via test_runner_with_uart.sh. */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

typedef union { float f; unsigned int u; } fb;
typedef union { double d; unsigned int w[2]; } db;

extern void t9_fprem_sn(unsigned int *io);
extern void t9_fprange_sn(unsigned int *io);
extern void t9_fplg_sn(unsigned int *io);
extern void t9_fpabs_sqrt_sn(unsigned int *io);
extern void t9_fpadddbsn(unsigned int *io);
extern void t9_fpchki32(unsigned int *io);
extern void t9_checkops(unsigned int *io);
extern void t9_dev_roundtrip(unsigned int *io);
extern void t9_ldprodid(unsigned int *io);
extern void t9_semaphore(unsigned int *io);
extern void t9_slice_ops(unsigned int *io);
extern void t9_fpstate(unsigned int *io);
extern void t9_stmove2dinit(unsigned int *io);
extern void t9_swapqueue(unsigned int *io);
extern void t9_flags(unsigned int *io);

static unsigned int io[16];
static int pass = 1;

static void check(const char *name, int ok)
{
    uart_puts(name);
    uart_puts(ok ? ":P " : ":F ");
    if (!ok) pass = 0;
}

static unsigned int fbits(float f) { fb b; b.f = f; return b.u; }

void test_main(void)
{
    fb f;
    db d;
    int i;

    /* fprem: 7.5 REM 2.0 = -0.5 (IEEE remainder, nearest quotient 4) */
    for (i = 0; i < 16; i++) io[i] = 0;
    io[0] = fbits(7.5f); io[1] = fbits(2.0f);
    t9_fprem_sn(io);
    check("fprem", io[2] == fbits(-0.5f));

    /* fprange: 7.5 RANGE 2.0 -> quotient 4.0, remainder -0.5 */
    for (i = 0; i < 16; i++) io[i] = 0;
    io[0] = fbits(7.5f); io[1] = fbits(2.0f);
    t9_fprange_sn(io);
    check("fprange", io[2] == fbits(4.0f) && io[3] == fbits(-0.5f));

    /* fplg: 1.0 <> 2.0 true; 1.0 <> 1.0 false; NaN <> 1.0 false */
    io[0] = fbits(1.0f); io[1] = fbits(2.0f); io[2] = 99;
    t9_fplg_sn(io);
    check("fplg1", io[2] == 1);
    io[0] = fbits(1.0f); io[1] = fbits(1.0f); io[2] = 99;
    t9_fplg_sn(io);
    check("fplg0", io[2] == 0);
    io[0] = 0x7FC00000u; io[1] = fbits(1.0f); io[2] = 99;
    t9_fplg_sn(io);
    check("fplgN", io[2] == 0);

    /* fpabs + fpsqrt: |-9.0| = 9.0, sqrt = 3.0 */
    io[0] = fbits(-9.0f);
    t9_fpabs_sqrt_sn(io);
    check("fpabs", io[1] == fbits(9.0f));
    check("fpsqrt", io[2] == fbits(3.0f));

    /* fpadddbsn: (0.1 + 0.2) as double sum, correctly rounded to single */
    d.d = 0.1; io[0] = d.w[0]; io[1] = d.w[1];
    d.d = 0.2; io[2] = d.w[0]; io[3] = d.w[1];
    t9_fpadddbsn(io);
    f.f = (float)(0.1 + 0.2);
    check("fpadddbsn", io[4] == f.u);

    /* fpchki32: 100.0f fits in int32 -> no error */
    io[0] = fbits(100.0f); io[1] = 0;
    t9_fpchki32(io);
    check("fpchki32", io[1] == 1);

    /* cb/cbu/cs/csu: 127 ok / 300 too big for u8 / 40000 too big for s16 /
     * 70000 too big for u16 */
    io[0] = 127; io[1] = 300; io[2] = 40000; io[3] = 70000;
    t9_checkops(io);
    check("cb", io[4] == 1);
    check("cbu", io[5] == 0);
    check("cs", io[6] == 0);
    check("csu", io[7] == 0);

    /* devsw/devlw roundtrip */
    io[0] = 0xC0DEF00Du; io[1] = 0; io[2] = 0;
    t9_dev_roundtrip(io);
    check("dev", io[1] == 0xC0DEF00Du && io[2] == 0xC0DEF00Du);

    /* ldprodid returns some identity without erroring */
    io[0] = 0xDEADBEEFu;
    t9_ldprodid(io);
    check("prodid", io[0] != 0xDEADBEEFu);

    /* semaphore: count 0 -> signal -> 1 -> wait -> 0, no deschedule */
    io[0] = 0; io[1] = 0x80000000u; io[2] = 0x80000000u;
    t9_semaphore(io);
    check("sem", io[0] == 0 && io[1] == 0x80000000u);

    /* settimeslice/intdis/intenb/timeslice/nop/icl/fdca */
    io[0] = 99;
    t9_slice_ops(io);
    check("slice", io[0] == 1);

    /* fpstall/fpldall roundtrip: FA=2.5(double), FB=1.5f(single) */
    for (i = 0; i < 16; i++) io[i] = 0;
    f.f = 1.5f; io[7] = f.u;
    d.d = 2.5;  io[8] = d.w[0]; io[9] = d.w[1];
    t9_fpstate(io);
    check("fpstate", io[10] == io[8] && io[11] == io[9] && io[12] == io[7]);

    /* stmove2dinit after move2dinit(5, 9, 7) */
    io[0] = io[1] = io[2] = 0;
    t9_stmove2dinit(io);
    check("stm2d", io[0] == 5 && io[1] == 9 && io[2] == 7);

    /* swapqueue on an empty low-priority list returns NotProcess.p */
    io[0] = 0;
    t9_swapqueue(io);
    check("swapq", io[0] == 0x80000000u);

    /* stflags(0); ldflags -> 0 */
    io[0] = 99;
    t9_flags(io);
    check("flags", io[0] == 0);

    uart_puts(pass ? "\nALL-PASS\n" : "\nFAILED\n");
}
