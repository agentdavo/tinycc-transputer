/* test_reloc_extern_static.c - an extern must never bind to another TU's static
 *
 * The helper defines static functions named "shared" and "only_static".
 * This file defines the global "shared" and refers to "only_static" only
 * through a weak declaration.  A global reference has to resolve to a global
 * definition: "shared" here, and nothing (0) for "only_static", whatever
 * locals of the same name other objects carry.  The linker's by-name
 * relocation repair used to fall back to such a local, so an extern with no
 * definition silently called another file's static instead.
 *
 * Expected: SXSOK\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }

int shared(void)
{
    return 2;
}

extern int only_static(void) __attribute__((weak));
extern int helper_shared(void);
extern int (*helper_keep)(void);

/* R_TRANSPUTER_32 references, one global and one weak. */
static int (*const tab[])(void) = { shared, only_static };

void test_main(void)
{
    int ok = 1;

    if (shared() != 2 || tab[0]() != 2)    /* IPREL32 and data32 */
        ok = 0;
    if (helper_shared() != 1)              /* the helper keeps its static */
        ok = 0;
    if (tab[1] != 0)                       /* weak, no global definition */
        ok = 0;
    if (helper_keep() != 7)
        ok = 0;

    if (ok) {
        uart_putc('X');
        uart_putc('S');
        uart_putc('O');
        uart_putc('K');
    } else {
        uart_putc('F');
        uart_putc('A');
        uart_putc('I');
        uart_putc('L');
    }
    uart_putc('\n');
}
