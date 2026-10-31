/*
 * test_large_switch.c — Test switch/case and goto in large functions.
 *
 * Hush's parser uses massive switch statements and goto labels.
 * This tests whether TCC generates correct CJ/J offsets for large
 * switch tables and forward/backward jumps.
 *
 * UART oracle: LARGE_SWITCH:PASS
 * Expected: ST:SW:OK\nT:GOTO:OK\nT:CSG:OK\nLARGE_SWITCH:PASS\nH
 */
#define UART (*(volatile unsigned int *)0x10000000u)

static void uart_puts(const char *s) { while (*s) UART = *s++; }
static void uart_putc(char c) { UART = (unsigned int)c; }
static void uart_hex8(unsigned v) {
    static const char h[] = "0123456789ABCDEF";
    UART = h[(v >> 4) & 0xF]; UART = h[v & 0xF];
}

/* Large switch with many cases — exercises jump table/CJ chain */
static int big_switch(int val) {
    int result = -1;
    switch (val) {
        case 0: result = 100; break;
        case 1: result = 101; break;
        case 2: result = 102; break;
        case 3: result = 103; break;
        case 4: result = 104; break;
        case 5: result = 105; break;
        case 6: result = 106; break;
        case 7: result = 107; break;
        case 8: result = 108; break;
        case 9: result = 109; break;
        case 10: result = 110; break;
        case 11: result = 111; break;
        case 12: result = 112; break;
        case 13: result = 113; break;
        case 14: result = 114; break;
        case 15: result = 115; break;
        case 16: result = 116; break;
        case 17: result = 117; break;
        case 18: result = 118; break;
        case 19: result = 119; break;
        case 20: result = 120; break;
        case 21: result = 121; break;
        case 22: result = 122; break;
        case 23: result = 123; break;
        case 24: result = 124; break;
        case 25: result = 125; break;
        case 26: result = 126; break;
        case 27: result = 127; break;
        case 28: result = 128; break;
        case 29: result = 129; break;
        case 30: result = 130; break;
        case 31: result = 131; break;
        default: result = 999; break;
    }
    return result;
}

/* Test goto: forward and backward jumps with many locals in between */
static int goto_test(int n) {
    int a = 0, b = 0, c = 0, d = 0, e = 0;
    int f = 0, g = 0, h = 0, i = 0, j = 0;
    int k = 0, l = 0, m = 0, p = 0, q = 0;

    if (n == 0) goto done;
    a = 1; b = 2; c = 3; d = 4; e = 5;

    if (n == 1) goto sum;
    f = 6; g = 7; h = 8; i = 9; j = 10;

    if (n == 2) goto sum;
    k = 11; l = 12; m = 13; p = 14; q = 15;

sum:
    return a+b+c+d+e+f+g+h+i+j+k+l+m+p+q;

done:
    return -1;
}

/* Large function with interleaved switch, goto, and locals */
static int complex_switch_goto(int op, int x, int y) {
    int r = 0;
    int t0 = 0, t1 = 0, t2 = 0, t3 = 0;
    int t4 = 0, t5 = 0, t6 = 0, t7 = 0;

    switch (op) {
        case 0:
            t0 = x + y;
            t1 = x - y;
            if (t1 < 0) goto negate;
            r = t0 + t1;
            break;
        case 1:
            t2 = x * 2;
            t3 = y * 3;
            r = t2 + t3;
            break;
        case 2:
            t4 = x;
            t5 = y;
            /* Swap via temps */
            t6 = t4; t4 = t5; t5 = t6;
            r = t4 * 10 + t5;
            break;
        case 3:
            t7 = 0;
            {
                int i;
                for (i = 0; i < x; i++)
                    t7 += y;
            }
            r = t7;
            break;
        default:
            r = -1;
            break;
    }
    goto done;

negate:
    r = -(x - y);
    goto done;

done:
    return r;
}

void test_main(void) {
    int pass = 1;
    int result;

    /* Test big_switch for all cases */
    uart_puts("T:SW:");
    {
        int fail = 0;
        int i;
        for (i = 0; i <= 31; i++) {
            result = big_switch(i);
            if (result != 100 + i) { fail = 1; break; }
        }
        result = big_switch(99);
        if (result != 999) fail = 1;
        if (!fail) uart_puts("OK\n");
        else { uart_puts("FAIL\n"); pass = 0; }
    }

    /* Test goto */
    uart_puts("T:GOTO:");
    if (goto_test(0) == -1 &&
        goto_test(1) == 15 &&     /* a..e = 1+2+3+4+5 = 15 */
        goto_test(2) == 55 &&     /* a..j = 1+..+10 = 55 */
        goto_test(3) == 120) {    /* a..q = 1+..+15 = 120 */
        uart_puts("OK\n");
    } else {
        uart_puts("FAIL:");
        uart_hex8(goto_test(0) & 0xFF);
        uart_putc('/');
        uart_hex8(goto_test(1) & 0xFF);
        uart_putc('/');
        uart_hex8(goto_test(2) & 0xFF);
        uart_putc('/');
        uart_hex8(goto_test(3) & 0xFF);
        uart_puts("\n");
        pass = 0;
    }

    /* Test complex_switch_goto */
    uart_puts("T:CSG:");
    if (complex_switch_goto(0, 10, 5) == 20 &&   /* t0=15,t1=5, r=15+5=20 */
        complex_switch_goto(0, 5, 10) == 5 &&     /* t0=15,t1=-5 → negate → r=5 */
        complex_switch_goto(1, 7, 3) == 23 &&     /* 14+9 = 23 */
        complex_switch_goto(2, 3, 7) == 73 &&     /* swap: t4=7,t5=3, r=73 */
        complex_switch_goto(3, 5, 4) == 20 &&     /* 5*4 = 20 */
        complex_switch_goto(9, 0, 0) == -1) {     /* default */
        uart_puts("OK\n");
    } else {
        uart_puts("FAIL:");
        uart_hex8(complex_switch_goto(0, 10, 5) & 0xFF);
        uart_puts("\n");
        pass = 0;
    }

    if (pass)
        uart_puts("LARGE_SWITCH:PASS\n");
    else
        uart_puts("LARGE_SWITCH:FAIL\n");
}
