/* test_xcall_main.c - Cross-file relocation and call regression test
 *
 * Build example:
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_boot.o transputer/test_boot.S
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_xcall_main.o transputer/test_xcall_main.c
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_xcall_h1.o transputer/test_xcall_helper1.c
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_xcall_h2.o transputer/test_xcall_helper2.c
 *   ./transputer-tcc -nostdlib -nostdinc -o /tmp/test_xcall.elf \
 *       /tmp/test_boot.o /tmp/test_xcall_main.o /tmp/test_xcall_h1.o /tmp/test_xcall_h2.o
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static void uart_hex(unsigned long v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

extern unsigned long helper1_tail_via_call(int idx);
extern unsigned long helper1_tail_via_fp(int idx);
extern unsigned long helper1_fill_and_sum(unsigned char *buf, unsigned long count);
extern unsigned long helper1_mix(unsigned long x);
extern unsigned long helper1_magic3(void);
extern unsigned long helper2_base(void);
extern const unsigned long helper_magic[4];

static void print_result(const char *id, unsigned long calc, unsigned long exp)
{
    uart_puts(id);
    uart_puts(" calc=");
    uart_hex(calc);
    uart_puts(" exp=");
    uart_hex(exp);
    uart_putc(calc == exp ? 'P' : 'F');
    uart_putc('\n');
}

void test_main(void)
{
    unsigned long base;
    unsigned long exp;
    unsigned long calc;
    unsigned long i;
    unsigned char buf[64];
    unsigned long seed = 0x01020304u;

    uart_puts("XCALL TEST\n");

    base = helper2_base();
    uart_puts("base="); uart_hex(base); uart_putc('\n');

    exp = base + 2048u + (3u * 20u) + 16u;
    calc = helper1_tail_via_call(3);
    print_result("L[0]", calc, exp);

    calc = helper1_tail_via_fp(4);
    exp = base + 2048u + (4u * 20u) + 16u;
    print_result("M[0]", calc, exp);

    calc = helper1_fill_and_sum(buf, sizeof(buf));
    exp = sizeof(buf);
    for (i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0x3cu) {
            calc = 0;
            break;
        }
    }
    print_result("N[0]", calc, exp);

    calc = helper1_mix(seed);
    exp = (seed ^ 0x55667788u) + 0x99aabbccu;
    print_result("O[0]", calc, exp);

    calc = helper_magic[0];
    exp = 0x10203040u;
    print_result("P[0]", calc, exp);

    calc = helper1_magic3();
    exp = 0xddeeff00u;
    print_result("Q[0]", calc, exp);

    uart_puts("DONE\n");
}
