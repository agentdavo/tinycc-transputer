/* Test bitwise operations that TCC miscompiles for transputer.
 * Known failures:
 *  - BUG_ON(align & (align - 1))  with align=32  (should be 0, evaluates non-zero)
 *  - BUG_ON(x & ~PAGE_MASK)       with x=0x8000  (should be 0, evaluates non-zero)
 */

/* UART MMIO - direct register access */
#define UART_DATA  (*(volatile unsigned long *)0x10000000)
#define UART_STAT  (*(volatile unsigned long *)0x10000004)

static void uart_putc(int c) {
    while (UART_STAT & 2) {}
    UART_DATA = (unsigned long)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_hex(unsigned long v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

/* Halt processor */
static void halt(void) {
    *(volatile unsigned long *)0x80000020 = 0; /* stopp */
    while(1) {}
}

void test_main(void) {
    unsigned long a, b, r;

    uart_puts("BITWISE TEST\n");

    /* Test 1: Simple AND */
    a = 32;
    b = 31;
    r = a & b;
    uart_puts("T1: 32&31=");
    uart_hex(r);
    uart_putc(r == 0 ? 'P' : 'F');
    uart_putc('\n');

    /* Test 2: x & (x-1) pattern */
    a = 32;
    r = a & (a - 1);
    uart_puts("T2: 32&(32-1)=");
    uart_hex(r);
    uart_putc(r == 0 ? 'P' : 'F');
    uart_putc('\n');

    /* Test 3: Bitwise NOT */
    a = 0x00000FFF;
    r = ~a;
    uart_puts("T3: ~0xFFF=");
    uart_hex(r);
    uart_putc(r == 0xFFFFF000 ? 'P' : 'F');
    uart_putc('\n');

    /* Test 4: x & ~y pattern */
    a = 0x8000;
    b = 0xFFFFF000;  /* PAGE_MASK */
    r = a & ~b;
    uart_puts("T4: 0x8000&~PAGE_MASK=");
    uart_hex(r);
    uart_putc(r == 0 ? 'P' : 'F');
    uart_putc('\n');

    /* Test 5: Combined expression like BUG_ON */
    a = 0x8000;
    r = a & 0xFFF;  /* ~PAGE_MASK as constant */
    uart_puts("T5: 0x8000&0xFFF=");
    uart_hex(r);
    uart_putc(r == 0 ? 'P' : 'F');
    uart_putc('\n');

    /* Test 6: if (x & y) branch test */
    a = 32;
    b = 31;
    uart_puts("T6: if(32&31)=");
    if (a & b) {
        uart_puts("BRANCH(F)\n");
    } else {
        uart_puts("NOBRANCH(P)\n");
    }

    /* Test 7: if (x & (x-1)) branch test */
    a = 32;
    uart_puts("T7: if(32&(32-1))=");
    if (a & (a - 1)) {
        uart_puts("BRANCH(F)\n");
    } else {
        uart_puts("NOBRANCH(P)\n");
    }

    /* Test 8: BUG_ON-like pattern with unsigned long */
    {
        unsigned long align = 32;
        unsigned long check = align & (align - 1);
        uart_puts("T8: align&(align-1) check=");
        uart_hex(check);
        uart_putc('\n');
        uart_puts("T8: if(check)=");
        if (check) {
            uart_puts("TRUE(F)\n");
        } else {
            uart_puts("FALSE(P)\n");
        }
    }

    /* Test 9: Test with volatile to prevent constant folding */
    {
        volatile unsigned long va = 32;
        volatile unsigned long vb = 31;
        unsigned long vr = va & vb;
        uart_puts("T9: vol 32&31=");
        uart_hex(vr);
        uart_putc(vr == 0 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 10: ~PAGE_MASK with volatile */
    {
        volatile unsigned long vmask = 0xFFFFF000;
        unsigned long vnot = ~vmask;
        uart_puts("T10: ~vol_mask=");
        uart_hex(vnot);
        uart_putc(vnot == 0xFFF ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 11: single ! on zero */
    {
        unsigned long x = 0;
        unsigned long r = !x;
        uart_puts("T11a: !0=");
        uart_hex(r);
        uart_putc(r == 1 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 11b: single ! on non-zero */
    {
        unsigned long x = 42;
        unsigned long r = !x;
        uart_puts("T11b: !42=");
        uart_hex(r);
        uart_putc(r == 0 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 11c: !! on zero */
    {
        unsigned long x = 0;
        unsigned long r = !!x;
        uart_puts("T11c: !!0=");
        uart_hex(r);
        uart_putc(r == 0 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 12: !! on non-zero */
    {
        unsigned long x = 42;
        unsigned long r = !!x;
        uart_puts("T12: !!42=");
        uart_hex(r);
        uart_putc(r == 1 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 13: !! on result of AND that should be zero */
    {
        unsigned long a = 32;
        unsigned long r = !!(a & (a - 1));
        uart_puts("T13: !!(32&31)=");
        uart_hex(r);
        uart_putc(r == 0 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 14: if (!!(x & y)) branch - like BUG_ON */
    {
        unsigned long a = 32;
        uart_puts("T14: if(!!(32&31))=");
        if (!!(a & (a - 1))) {
            uart_puts("BRANCH(F)\n");
        } else {
            uart_puts("NOBRANCH(P)\n");
        }
    }

    /* Test 15: __builtin_expect(!!(x), 0) pattern */
    {
        unsigned long a = 32;
        uart_puts("T15: builtin_expect(!!(32&31),0)=");
        if (__builtin_expect(!!(a & (a - 1)), 0)) {
            uart_puts("BRANCH(F)\n");
        } else {
            uart_puts("NOBRANCH(P)\n");
        }
    }

    /* Test 16: Exactly the BUG_ON pattern from kernel */
    {
        volatile unsigned long unit_size = 0x8000;
        unsigned long page_mask = ~(4096UL - 1);
        uart_puts("T16: BUG_ON(unit_size & ~PAGE_MASK)=");
        if (__builtin_expect(!!(unit_size & ~page_mask), 0)) {
            uart_puts("BUG(F)\n");
        } else {
            uart_puts("OK(P)\n");
        }
    }

    /* Test 17: struct member access pattern like ai->unit_size */
    {
        struct { unsigned long unit_size; } s;
        unsigned long page_mask = ~(4096UL - 1);
        s.unit_size = 0x8000;
        uart_puts("T17: s.unit_size&~PAGE_MASK=");
        uart_hex(s.unit_size & ~page_mask);
        uart_puts(" BUG_ON=");
        if (__builtin_expect(!!(s.unit_size & ~page_mask), 0)) {
            uart_puts("BUG(F)\n");
        } else {
            uart_puts("OK(P)\n");
        }
    }

    uart_puts("DONE\n");
    halt();
}
