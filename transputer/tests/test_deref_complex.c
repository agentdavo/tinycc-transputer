/*
 * test_deref_complex.c - Complex double-deref patterns
 *
 * Tests patterns that stress the eval stack and spilling behavior
 * for double-pointer dereferences in larger expressions.
 *
 * Uses test_boot.S entry (calls test_main).
 * Expected: ST1:OK\nT1b:OK\nT2:OK\nT3:OK\nT4:OK\nT5:OK\nT6:OK\nT7:OK\nPASS\n
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)
#define UART_STAT (*(volatile unsigned long *)0x10000004)

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
 * TEST 1: Double deref in conditional expression
 * ============================================================ */
static int __attribute__((noinline)) test_deref_in_cond(int **pp) {
    /* if (*pp) return **pp else return -1 */
    if (*pp)
        return **pp;
    return -1;
}

/* ============================================================
 * TEST 2: Double deref with array index (argptr[i] pattern)
 * ============================================================ */
static int __attribute__((noinline)) test_argptr_indexed(void **argptr, int idx) {
    return *((int *)(argptr[idx]));
}

/* ============================================================
 * TEST 3: Cast chain: void* array -> specific type
 * This is the EXACT _vfprintf pattern:
 *   const void * argptr[N];
 *   argptr[i] = (void*)p++;
 *   s = *((char **) (*argptr));
 * ============================================================ */
typedef union {
    unsigned int u;
    unsigned long ul;
    void *p;
} argvalue_t;

static char * __attribute__((noinline)) test_vfprintf_argptr(void) {
    char *str = "hello";
    argvalue_t av;
    const void *argptr[4];

    /* Simulate GET_VA_ARG: store pointer into union */
    av.p = (void *)&str;

    /* Simulate argptr setup: argptr[0] = &av */
    argptr[0] = (void *)&av;

    /* Simulate _do_one_spec: s = *((char **) (*argptr))
     * *argptr = argptr[0] = &av = pointer to union
     * (char **) = cast to char**
     * * = dereference = read first word of union = av.p = &str
     * Wait, that's wrong. av.p = &str, so *(char**)(&av) = *(&str) through the union... no.
     *
     * Actually: av.p stores a void* pointing to the char* variable.
     * *argptr = (void*)&av. Cast to char** gives (char**)&av.
     * Deref gives av.p interpreted as char* = &str.
     * But that's the address of str, not the value of str.
     *
     * Actually in the real code:
     * GET_VA_ARG(p,p,void *,ppfs->arg) does: p->p = va_arg(ap, void*) = str's value
     * Then argptr[i] = (void*)p++  → argptr[0] = &av
     * Then *((char**)(*argptr)) = *((char**)&av) = av.p cast to char* = str's value directly!
     *
     * So av.p already has the string pointer value (not the address of a pointer).
     */
    av.p = (void *)str;  /* Store the actual string pointer value */
    argptr[0] = (void *)&av;

    /* The vfprintf pattern */
    return *((char **) (*argptr));
}

/* ============================================================
 * TEST 4: Multiple argptr accesses in sequence
 * ============================================================ */
static void __attribute__((noinline)) test_multi_argptr(void **argptr,
                                                         char **out1,
                                                         char **out2) {
    *out1 = *((char **) (argptr[0]));
    *out2 = *((char **) (argptr[1]));
}

/* ============================================================
 * TEST 5: Double deref through volatile (prevent optimization)
 * ============================================================ */
static int __attribute__((noinline)) test_volatile_deref(void) {
    int val = 77;
    int *p = &val;
    volatile int **vpp = (volatile int **)&p;
    return **vpp;
}

/* ============================================================
 * TEST 6: Double deref with struct member through pointer
 * (simulates ppfs->argptr access pattern)
 * ============================================================ */
struct fake_ppfs {
    int dummy1;
    int dummy2;
    const void *argptr[4];
    int num_data_args;
};

static char * __attribute__((noinline)) test_ppfs_deref(struct fake_ppfs *ppfs) {
    return *((char **) (*ppfs->argptr));
}

/* ============================================================
 * TEST 7: Nested function call with double deref result
 * ============================================================ */
static int __attribute__((noinline)) use_string(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int __attribute__((noinline)) test_deref_into_call(void **argptr) {
    char *s = *((char **) (*argptr));
    return use_string(s);
}

int test_main(void) {
    int ok = 1;

    /* Test 1 */
    {
        int val = 55;
        int *p = &val;
        int r = test_deref_in_cond(&p);
        if (r == 55) {
            uart_puts("T1:OK\n");
        } else {
            uart_puts("T1:FAIL ");
            uart_hex(r);
            uart_putc('\n');
            ok = 0;
        }
    }

    /* Test 1b: NULL case */
    {
        int *p = 0;
        int r = test_deref_in_cond(&p);
        if (r == -1) {
            uart_puts("T1b:OK\n");
        } else {
            uart_puts("T1b:FAIL ");
            uart_hex(r);
            uart_putc('\n');
            ok = 0;
        }
    }

    /* Test 2: indexed argptr */
    {
        int a = 10, b = 20;
        void *arr[2];
        arr[0] = (void *)&a;
        arr[1] = (void *)&b;
        int r0 = test_argptr_indexed(arr, 0);
        int r1 = test_argptr_indexed(arr, 1);
        if (r0 == 10 && r1 == 20) {
            uart_puts("T2:OK\n");
        } else {
            uart_puts("T2:FAIL r0=");
            uart_hex(r0);
            uart_puts(" r1=");
            uart_hex(r1);
            uart_putc('\n');
            ok = 0;
        }
    }

    /* Test 3: vfprintf argptr pattern */
    {
        char *s = test_vfprintf_argptr();
        if (s && s[0] == 'h' && s[1] == 'e' && s[4] == 'o') {
            uart_puts("T3:OK\n");
        } else {
            uart_puts("T3:FAIL ptr=");
            uart_hex((unsigned long)s);
            uart_putc('\n');
            ok = 0;
        }
    }

    /* Test 4: multi-argptr */
    {
        char *s1 = "alpha";
        char *s2 = "beta";
        argvalue_t av[2];
        void *argptr[2];
        char *out1, *out2;

        av[0].p = (void *)s1;
        av[1].p = (void *)s2;
        argptr[0] = (void *)&av[0];
        argptr[1] = (void *)&av[1];

        test_multi_argptr(argptr, &out1, &out2);
        if (out1 == s1 && out2 == s2) {
            uart_puts("T4:OK\n");
        } else {
            uart_puts("T4:FAIL out1=");
            uart_hex((unsigned long)out1);
            uart_puts(" out2=");
            uart_hex((unsigned long)out2);
            uart_putc('\n');
            ok = 0;
        }
    }

    /* Test 5: volatile */
    {
        int r = test_volatile_deref();
        if (r == 77) {
            uart_puts("T5:OK\n");
        } else {
            uart_puts("T5:FAIL ");
            uart_hex(r);
            uart_putc('\n');
            ok = 0;
        }
    }

    /* Test 6: ppfs struct pattern */
    {
        struct fake_ppfs ppfs;
        char *str = "test6str";
        argvalue_t av;
        av.p = (void *)str;
        ppfs.argptr[0] = (void *)&av;
        ppfs.num_data_args = 1;

        char *s = test_ppfs_deref(&ppfs);
        if (s == str) {
            uart_puts("T6:OK\n");
        } else {
            uart_puts("T6:FAIL ptr=");
            uart_hex((unsigned long)s);
            uart_puts(" expected=");
            uart_hex((unsigned long)str);
            uart_putc('\n');
            ok = 0;
        }
    }

    /* Test 7: deref into function call */
    {
        char *str = "hello";
        argvalue_t av;
        av.p = (void *)str;
        void *argptr = (void *)&av;

        int len = test_deref_into_call(&argptr);
        if (len == 5) {
            uart_puts("T7:OK\n");
        } else {
            uart_puts("T7:FAIL len=");
            uart_hex(len);
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
