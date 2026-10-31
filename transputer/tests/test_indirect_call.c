/*
 * test_indirect_call.c — Test indirect function calls (via function pointer).
 *
 * Tests various patterns of calling functions through pointers:
 *   T1: Simple function pointer call (no args, no return)
 *   T2: Function pointer with args
 *   T3: Function pointer with return value
 *   T4: Function pointer from struct field
 *   T5: Function pointer passed as argument
 *   T6: Array of function pointers
 *   T7: Function pointer returning pointer
 *   T8: Nested indirect call (call through fnptr that calls through fnptr)
 *
 * Expected UART: T1:P\nT2:P\nT3:P\nT4:P\nT5:P\nT6:P\nT7:P\nT8:P\n
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

static void print_result(const char *tag, int pass) {
    uart_puts(tag);
    uart_putc(':');
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');
}

/* --- T1: Simple call, no args, no return --- */
static int t1_called = 0;
static void t1_func(void) {
    t1_called = 1;
}

/* --- T2: Call with args --- */
static int t2_result = 0;
static void t2_func(int a, int b, int c) {
    t2_result = a + b + c;
}

/* --- T3: Call with return value --- */
static int t3_func(int x) {
    return x * 3;
}

/* --- T4: Function pointer in struct --- */
struct ops {
    int (*compute)(int, int);
    void (*notify)(void);
};

static int t4_add(int a, int b) { return a + b; }
static int t4_notified = 0;
static void t4_notify(void) { t4_notified = 1; }

/* --- T5: Function pointer passed as argument --- */
static int apply(int (*fn)(int), int val) {
    return fn(val);
}
static int t5_double(int x) { return x + x; }

/* --- T6: Array of function pointers --- */
static int fn_a(void) { return 10; }
static int fn_b(void) { return 20; }
static int fn_c(void) { return 30; }

/* --- T7: Function pointer returning pointer --- */
static int t7_val = 99;
static int *t7_get_ptr(void) { return &t7_val; }

/* --- T8: Nested indirect calls --- */
static int t8_inner_val = 0;
static void t8_inner(void) { t8_inner_val = 42; }
typedef void (*void_fn)(void);

static void t8_outer(void_fn fn) {
    fn();
}

void test_main(void)
{
    /* T1: Simple indirect call, no args */
    {
        void (*fp)(void) = t1_func;
        fp();
        print_result("T1", t1_called == 1);
    }

    /* T2: Indirect call with 3 args */
    {
        void (*fp)(int, int, int) = t2_func;
        fp(10, 20, 30);
        print_result("T2", t2_result == 60);
    }

    /* T3: Indirect call with return value */
    {
        int (*fp)(int) = t3_func;
        int r = fp(7);
        print_result("T3", r == 21);
    }

    /* T4: Function pointer from struct */
    {
        struct ops o;
        o.compute = t4_add;
        o.notify = t4_notify;
        int sum = o.compute(15, 25);
        o.notify();
        print_result("T4", sum == 40 && t4_notified == 1);
    }

    /* T5: Function pointer passed as argument to another function */
    {
        int r = apply(t5_double, 13);
        print_result("T5", r == 26);
    }

    /* T6: Array of function pointers */
    {
        int (*fns[3])(void);
        fns[0] = fn_a;
        fns[1] = fn_b;
        fns[2] = fn_c;
        int total = fns[0]() + fns[1]() + fns[2]();
        print_result("T6", total == 60);
    }

    /* T7: Indirect call returning a pointer */
    {
        int *(*fp)(void) = t7_get_ptr;
        int *p = fp();
        print_result("T7", p == &t7_val && *p == 99);
    }

    /* T8: Nested indirect call (outer calls inner through pointer) */
    {
        t8_outer(t8_inner);
        print_result("T8", t8_inner_val == 42);
    }
}
