/*
 * test_large_func.c — Test workspace management for large functions.
 *
 * Tests increasingly large functions to find the threshold where
 * TCC's transputer backend miscompiles workspace offsets.
 *
 * UART oracle: LARGE_FUNC:PASS
 */
#define UART (*(volatile unsigned int *)0x10000000u)

static void uart_puts(const char *s) { while (*s) UART = *s++; }
static void uart_hex8(unsigned v) {
    static const char h[] = "0123456789ABCDEF";
    UART = h[(v >> 4) & 0xF]; UART = h[v & 0xF];
}

/*
 * Test: function with N local variables.
 * Each local is assigned a unique value, then all are summed.
 * If workspace offsets are wrong, the sum will be wrong.
 */

/* 20 locals — well within 3-byte encoding */
static int func_20_locals(void) {
    int a0=1,a1=2,a2=3,a3=4,a4=5,a5=6,a6=7,a7=8,a8=9,a9=10;
    int b0=11,b1=12,b2=13,b3=14,b4=15,b5=16,b6=17,b7=18,b8=19,b9=20;
    return a0+a1+a2+a3+a4+a5+a6+a7+a8+a9+b0+b1+b2+b3+b4+b5+b6+b7+b8+b9;
}

/* 40 locals */
static int func_40_locals(void) {
    int a0=1,a1=2,a2=3,a3=4,a4=5,a5=6,a6=7,a7=8,a8=9,a9=10;
    int b0=11,b1=12,b2=13,b3=14,b4=15,b5=16,b6=17,b7=18,b8=19,b9=20;
    int c0=21,c1=22,c2=23,c3=24,c4=25,c5=26,c6=27,c7=28,c8=29,c9=30;
    int d0=31,d1=32,d2=33,d3=34,d4=35,d5=36,d6=37,d7=38,d8=39,d9=40;
    return a0+a1+a2+a3+a4+a5+a6+a7+a8+a9+
           b0+b1+b2+b3+b4+b5+b6+b7+b8+b9+
           c0+c1+c2+c3+c4+c5+c6+c7+c8+c9+
           d0+d1+d2+d3+d4+d5+d6+d7+d8+d9;
}

/* 60 locals */
static int func_60_locals(void) {
    int a0=1,a1=2,a2=3,a3=4,a4=5,a5=6,a6=7,a7=8,a8=9,a9=10;
    int b0=11,b1=12,b2=13,b3=14,b4=15,b5=16,b6=17,b7=18,b8=19,b9=20;
    int c0=21,c1=22,c2=23,c3=24,c4=25,c5=26,c6=27,c7=28,c8=29,c9=30;
    int d0=31,d1=32,d2=33,d3=34,d4=35,d5=36,d6=37,d7=38,d8=39,d9=40;
    int e0=41,e1=42,e2=43,e3=44,e4=45,e5=46,e6=47,e7=48,e8=49,e9=50;
    int f0=51,f1=52,f2=53,f3=54,f4=55,f5=56,f6=57,f7=58,f8=59,f9=60;
    return a0+a1+a2+a3+a4+a5+a6+a7+a8+a9+
           b0+b1+b2+b3+b4+b5+b6+b7+b8+b9+
           c0+c1+c2+c3+c4+c5+c6+c7+c8+c9+
           d0+d1+d2+d3+d4+d5+d6+d7+d8+d9+
           e0+e1+e2+e3+e4+e5+e6+e7+e8+e9+
           f0+f1+f2+f3+f4+f5+f6+f7+f8+f9;
}

/* 80 locals — may be near the threshold */
static int func_80_locals(void) {
    int a0=1,a1=2,a2=3,a3=4,a4=5,a5=6,a6=7,a7=8,a8=9,a9=10;
    int b0=11,b1=12,b2=13,b3=14,b4=15,b5=16,b6=17,b7=18,b8=19,b9=20;
    int c0=21,c1=22,c2=23,c3=24,c4=25,c5=26,c6=27,c7=28,c8=29,c9=30;
    int d0=31,d1=32,d2=33,d3=34,d4=35,d5=36,d6=37,d7=38,d8=39,d9=40;
    int e0=41,e1=42,e2=43,e3=44,e4=45,e5=46,e6=47,e7=48,e8=49,e9=50;
    int f0=51,f1=52,f2=53,f3=54,f4=55,f5=56,f6=57,f7=58,f8=59,f9=60;
    int g0=61,g1=62,g2=63,g3=64,g4=65,g5=66,g6=67,g7=68,g8=69,g9=70;
    int h0=71,h1=72,h2=73,h3=74,h4=75,h5=76,h6=77,h7=78,h8=79,h9=80;
    return a0+a1+a2+a3+a4+a5+a6+a7+a8+a9+
           b0+b1+b2+b3+b4+b5+b6+b7+b8+b9+
           c0+c1+c2+c3+c4+c5+c6+c7+c8+c9+
           d0+d1+d2+d3+d4+d5+d6+d7+d8+d9+
           e0+e1+e2+e3+e4+e5+e6+e7+e8+e9+
           f0+f1+f2+f3+f4+f5+f6+f7+f8+f9+
           g0+g1+g2+g3+g4+g5+g6+g7+g8+g9+
           h0+h1+h2+h3+h4+h5+h6+h7+h8+h9;
}

/* Large function with control flow: if/else, loops, function calls */
static int complex_large_func(int n) {
    int accum = 0;
    int a0=0,a1=0,a2=0,a3=0,a4=0,a5=0,a6=0,a7=0,a8=0,a9=0;
    int b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0,b7=0,b8=0,b9=0;
    int c0=0,c1=0,c2=0,c3=0,c4=0,c5=0,c6=0,c7=0,c8=0,c9=0;
    int i;

    /* Fill with sequential values using a loop */
    for (i = 0; i < n; i++) {
        a0 += 1; a1 += 2; a2 += 3; a3 += 4; a4 += 5;
        a5 += 6; a6 += 7; a7 += 8; a8 += 9; a9 += 10;
        if (i & 1) {
            b0 += 1; b1 += 2; b2 += 3; b3 += 4; b4 += 5;
        } else {
            b5 += 6; b6 += 7; b7 += 8; b8 += 9; b9 += 10;
        }
        c0 += i;
    }

    /* Sum everything */
    accum = a0+a1+a2+a3+a4+a5+a6+a7+a8+a9;
    accum += b0+b1+b2+b3+b4+b5+b6+b7+b8+b9;
    accum += c0;

    return accum;
}

void test_main(void) {
    int result, expected;
    int pass = 1;

    uart_puts("T:20L:");
    result = func_20_locals();
    expected = 210; /* sum(1..20) */
    if (result == expected) { uart_puts("OK\n"); }
    else { uart_puts("FAIL:"); uart_hex8(result & 0xFF); uart_puts("\n"); pass = 0; }

    uart_puts("T:40L:");
    result = func_40_locals();
    expected = 820; /* sum(1..40) */
    if (result == expected) { uart_puts("OK\n"); }
    else { uart_puts("FAIL:"); uart_hex8(result & 0xFF); uart_puts("\n"); pass = 0; }

    uart_puts("T:60L:");
    result = func_60_locals();
    expected = 1830; /* sum(1..60) */
    if (result == expected) { uart_puts("OK\n"); }
    else { uart_puts("FAIL:"); uart_hex8(result & 0xFF); uart_puts("\n"); pass = 0; }

    uart_puts("T:80L:");
    result = func_80_locals();
    expected = 3240; /* sum(1..80) */
    if (result == expected) { uart_puts("OK\n"); }
    else { uart_puts("FAIL:"); uart_hex8(result & 0xFF); uart_puts("\n"); pass = 0; }

    uart_puts("T:CPX:");
    result = complex_large_func(10);
    /* a0-a9: 10*(1+2+...+10) = 10*55 = 550 */
    /* b0-b4: 5 iterations (odd i=1,3,5,7,9) * (1+2+3+4+5) = 5*15 = 75 */
    /* b5-b9: 5 iterations (even i=0,2,4,6,8) * (6+7+8+9+10) = 5*40 = 200 */
    /* c0: sum(0..9) = 45 */
    expected = 550 + 75 + 200 + 45; /* = 870 */
    if (result == expected) { uart_puts("OK\n"); }
    else { uart_puts("FAIL:"); uart_hex8((result >> 8) & 0xFF); uart_hex8(result & 0xFF); uart_puts("\n"); pass = 0; }

    if (pass)
        uart_puts("LARGE_FUNC:PASS\n");
    else
        uart_puts("LARGE_FUNC:FAIL\n");
}
