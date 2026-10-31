/*
 * test_indirect_call_advanced.c — Advanced indirect function call tests.
 *
 * Tests patterns commonly found in the Linux kernel:
 *   T1: Indirect call with >3 args (stack-passed args)
 *   T2: Indirect call through volatile pointer
 *   T3: Indirect call chain (A calls B via ptr, B calls C via ptr)
 *   T4: Recursive indirect call (function calls itself via pointer)
 *   T5: Indirect call with struct-by-value argument
 *   T6: Indirect call from conditional expression
 *   T7: Initcall-style function pointer table walk
 *   T8: Callback with context pointer (kernel pattern)
 *   T9: Indirect call where pointer comes from global
 *   T10: Indirect call with 64-bit (long long) return
 *
 * Expected UART: T1:P\nT2:P\nT3:P\nT4:P\nT5:P\nT6:P\nT7:P\nT8:P\nT9:P\nT10:P\n
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

/* --- T1: >3 args (requires stack passing) --- */
static int t1_sum5(int a, int b, int c, int d, int e) {
    return a + b + c + d + e;
}

/* --- T2: volatile function pointer --- */
static int t2_val = 0;
static void t2_set(int v) { t2_val = v; }

/* --- T3: Call chain --- */
static int t3_result = 0;
typedef int (*unary_fn)(int);

static int t3_multiply(int x) { return x * 2; }

static int t3_chain(unary_fn fn, int val) {
    return fn(val) + 1;
}

/* --- T4: Recursive through pointer --- */
static int t4_factorial(int n) {
    int (*self)(int) = t4_factorial;
    if (n <= 1) return 1;
    return n * self(n - 1);
}

/* --- T5: Struct-by-value arg --- */
struct point { int x; int y; };

static int t5_distance_sq(struct point p) {
    return p.x * p.x + p.y * p.y;
}

/* --- T6: Conditional expression --- */
static int t6_add(int a, int b) { return a + b; }
static int t6_sub(int a, int b) { return a - b; }

/* --- T7: Initcall table walk --- */
typedef void (*initcall_t)(void);
static int t7_count = 0;
static void t7_init_a(void) { t7_count += 1; }
static void t7_init_b(void) { t7_count += 10; }
static void t7_init_c(void) { t7_count += 100; }

/* --- T8: Callback with context --- */
struct callback_ctx {
    int accumulator;
    void (*handler)(struct callback_ctx *ctx, int value);
};

static void t8_handler(struct callback_ctx *ctx, int value) {
    ctx->accumulator += value;
}

/* --- T9: Indirect through global --- */
static int (*g_fn)(int, int) = 0;
static int t9_mul(int a, int b) { return a * b; }

/* --- T10: 64-bit return --- */
static long long t10_big(int a, int b) {
    long long la = a;
    long long lb = b;
    return la * lb;
}

void test_main(void)
{
    /* T1: >3 args through function pointer */
    {
        int (*fp)(int, int, int, int, int) = t1_sum5;
        int r = fp(1, 2, 3, 4, 5);
        print_result("T1", r == 15);
    }

    /* T2: Call through volatile function pointer */
    {
        void (* volatile vfp)(int) = t2_set;
        vfp(77);
        print_result("T2", t2_val == 77);
    }

    /* T3: Chain: test_main -> t3_chain(ptr) -> t3_multiply(ptr) */
    {
        int r = t3_chain(t3_multiply, 10);
        print_result("T3", r == 21);
    }

    /* T4: Recursive through function pointer */
    {
        int r = t4_factorial(5);
        print_result("T4", r == 120);
    }

    /* T5: Struct-by-value through function pointer */
    {
        int (*fp)(struct point) = t5_distance_sq;
        struct point p;
        p.x = 3;
        p.y = 4;
        int r = fp(p);
        print_result("T5", r == 25);
    }

    /* T6: Conditional function pointer selection */
    {
        int flag = 1;
        int (*fp)(int, int) = flag ? t6_add : t6_sub;
        int r1 = fp(10, 3);
        fp = (!flag) ? t6_add : t6_sub;
        int r2 = fp(10, 3);
        print_result("T6", r1 == 13 && r2 == 7);
    }

    /* T7: Initcall-style table walk */
    {
        initcall_t table[3];
        initcall_t *p;
        table[0] = t7_init_a;
        table[1] = t7_init_b;
        table[2] = t7_init_c;
        for (p = table; p < table + 3; p++) {
            (*p)();
        }
        print_result("T7", t7_count == 111);
    }

    /* T8: Callback with context pointer */
    {
        struct callback_ctx ctx;
        ctx.accumulator = 0;
        ctx.handler = t8_handler;
        ctx.handler(&ctx, 5);
        ctx.handler(&ctx, 15);
        ctx.handler(&ctx, 30);
        print_result("T8", ctx.accumulator == 50);
    }

    /* T9: Indirect call through global pointer */
    {
        g_fn = t9_mul;
        int r = g_fn(6, 7);
        print_result("T9", r == 42);
    }

    /* T10: 64-bit return through function pointer */
    {
        long long (*fp)(int, int) = t10_big;
        long long r = fp(100000, 100000);
        /* 100000 * 100000 = 10000000000 = 0x2_540B_E400 */
        int lo = (int)(r & 0xFFFFFFFF);
        int hi = (int)((r >> 32) & 0xFFFFFFFF);
        print_result("T10", lo == (int)0x540BE400 && hi == 2);
    }
}
