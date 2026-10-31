/* Test double-pointer dereference patterns for transputer backend.
 *
 * The vsnprintf codegen bug:  *((char **) (*argptr))
 * requires two consecutive dereferences through the eval stack.
 *
 * Expected: ST1:OK\nT2:OK\nT3:OK\nT4:OK\nT5:OK\nT6:OK\nT7:OK\nPASS\n
 */

/* UART MMIO */
#define UART_DATA  (*(volatile unsigned long *)0x10000000)
#define UART_STAT  (*(volatile unsigned long *)0x10000004)

static void uart_putc(int c) {
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

static void halt(void) {
    *(volatile unsigned long *)0x80000020 = 0;
    while(1) {}
}

/* ============================================================
 * TEST 1: Basic double dereference (int **pp)
 * ============================================================ */
static int __attribute__((noinline)) test_basic_double_deref(void) {
    int val = 42;
    int *p = &val;
    int **pp = &p;
    return **pp;  /* should return 42 */
}

/* ============================================================
 * TEST 2: Double deref through function parameter
 * ============================================================ */
static int __attribute__((noinline)) deref_pp(int **pp) {
    return **pp;
}

/* ============================================================
 * TEST 3: void** pattern (matches vsnprintf argptr)
 * ============================================================ */
static char * __attribute__((noinline)) get_str_from_argptr(void **argptr) {
    /* This is the exact pattern from vsnprintf:
     *   char *s = *((char **) (*argptr));
     * Steps:
     *   1. Load argptr (a pointer to void*)
     *   2. Deref *argptr to get the void* value
     *   3. Cast to char** (no-op in codegen)
     *   4. Deref again to get char*
     */
    return *((char **) (*argptr));
}

/* ============================================================
 * TEST 4: Double deref with offset (struct member)
 * ============================================================ */
struct args {
    int dummy;
    char *str;
};

static char * __attribute__((noinline)) deref_struct_ptr(struct args **pp) {
    return (*pp)->str;
}

/* ============================================================
 * TEST 5: Triple dereference (char ***)
 * ============================================================ */
static char __attribute__((noinline)) triple_deref(char ***ppp) {
    return ***ppp;
}

/* ============================================================
 * TEST 6: Double deref then use result as string
 * ============================================================ */
static void __attribute__((noinline)) print_from_argptr(void **argptr) {
    char *s = *((char **) (*argptr));
    uart_puts(s);
}

/* ============================================================
 * TEST 7: va_arg-like pattern with pointer arithmetic
 * ============================================================ */
static char * __attribute__((noinline)) va_arg_like(void **ap_ptr) {
    /* Simulate: char *s = va_arg(ap, char*);
     * which expands roughly to:
     *   void *ap = *ap_ptr;
     *   char *s = *(char **)ap;
     *   *ap_ptr = (void*)((char*)ap + 4);
     *   return s;
     */
    void *ap = *ap_ptr;
    char *s = *(char **)ap;
    *ap_ptr = (void *)((char *)ap + 4);
    return s;
}

int test_main(void) {
    int ok = 1;
    int result;

    /* Test 1: basic **pp */
    result = test_basic_double_deref();
    if (result == 42) {
        uart_puts("T1:OK\n");
    } else {
        uart_puts("T1:FAIL val=");
        uart_hex(result);
        uart_putc('\n');
        ok = 0;
    }

    /* Test 2: **pp through function param */
    {
        int val = 99;
        int *p = &val;
        result = deref_pp(&p);
        if (result == 99) {
            uart_puts("T2:OK\n");
        } else {
            uart_puts("T2:FAIL val=");
            uart_hex(result);
            uart_putc('\n');
            ok = 0;
        }
    }

    /* Test 3: void** pattern (vsnprintf) */
    {
        char *str = "hello";
        void *vp = (void *)&str;
        char *s = get_str_from_argptr(&vp);
        if (s == str) {
            uart_puts("T3:OK\n");
        } else {
            uart_puts("T3:FAIL ptr=");
            uart_hex((unsigned long)s);
            uart_puts(" expected=");
            uart_hex((unsigned long)str);
            uart_putc('\n');
            ok = 0;
        }
    }

    /* Test 4: struct through double deref */
    {
        struct args a;
        struct args *pa = &a;
        a.dummy = 0;
        a.str = "world";
        char *s = deref_struct_ptr(&pa);
        if (s == a.str) {
            uart_puts("T4:OK\n");
        } else {
            uart_puts("T4:FAIL ptr=");
            uart_hex((unsigned long)s);
            uart_putc('\n');
            ok = 0;
        }
    }

    /* Test 5: triple deref */
    {
        char ch = 'X';
        char *pc = &ch;
        char **ppc = &pc;
        char r = triple_deref(&ppc);
        if (r == 'X') {
            uart_puts("T5:OK\n");
        } else {
            uart_puts("T5:FAIL ch=");
            uart_putc(r);
            uart_putc('\n');
            ok = 0;
        }
    }

    /* Test 6: double deref + string print */
    {
        char *str = "T6:OK\n";
        void *vp = (void *)&str;
        print_from_argptr(&vp);
    }

    /* Test 7: va_arg-like pointer advance */
    {
        char *strings[2];
        strings[0] = "first";
        strings[1] = "second";
        void *ap = (void *)&strings[0];
        char *s1 = va_arg_like(&ap);
        char *s2 = va_arg_like(&ap);
        if (s1 == strings[0] && s2 == strings[1]) {
            uart_puts("T7:OK\n");
        } else {
            uart_puts("T7:FAIL s1=");
            uart_hex((unsigned long)s1);
            uart_puts(" s2=");
            uart_hex((unsigned long)s2);
            uart_putc('\n');
            ok = 0;
        }
    }

    if (ok)
        uart_puts("PASS\n");
    else
        uart_puts("FAIL\n");

    halt();
    return 0;
}
