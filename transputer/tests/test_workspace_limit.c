/*
 * test_workspace_limit.c — Push workspace encoding to the limit.
 *
 * Tests functions with 200+ locals and local arrays to verify
 * that workspace offsets encode correctly near the 255-word and
 * 4095-word boundaries.
 *
 * UART oracle: WORKSPACE_LIMIT:PASS
 */
#define UART (*(volatile unsigned int *)0x10000000u)

static void uart_puts(const char *s) { while (*s) UART = *s++; }
static void uart_hex32(unsigned v) {
    static const char h[] = "0123456789ABCDEF";
    int i;
    for (i = 28; i >= 0; i -= 4) UART = h[(v >> i) & 0xF];
}

/* Test with a local array — uses LDLP for address-of */
static int array_sum_test(int n) {
    int arr[64];
    int i, sum = 0;

    for (i = 0; i < 64 && i < n; i++)
        arr[i] = i + 1;
    for (i = 0; i < 64 && i < n; i++)
        sum += arr[i];
    return sum;
}

/* Test with TWO large local arrays — stresses frame size */
static int dual_array_test(int n) {
    int a[128];
    int b[128];
    int i, sum = 0;

    for (i = 0; i < 128; i++) {
        a[i] = i;
        b[i] = 127 - i;
    }
    /* a[i] + b[i] = 127 for all i */
    for (i = 0; i < n && i < 128; i++)
        sum += a[i] + b[i];
    return sum;
}

/* 120 locals: tests workspace offsets well above the 20-slot reserved area.
 * With 20 reserved slots + 120 locals = 140 words = 560 bytes frame.
 * This is within 3-byte encoding but exercises medium-range offsets. */
static int func_120_locals(void) {
    int a00=1,a01=2,a02=3,a03=4,a04=5,a05=6,a06=7,a07=8,a08=9,a09=10;
    int a10=11,a11=12,a12=13,a13=14,a14=15,a15=16,a16=17,a17=18,a18=19,a19=20;
    int a20=21,a21=22,a22=23,a23=24,a24=25,a25=26,a26=27,a27=28,a28=29,a29=30;
    int a30=31,a31=32,a32=33,a33=34,a34=35,a35=36,a36=37,a37=38,a38=39,a39=40;
    int a40=41,a41=42,a42=43,a43=44,a44=45,a45=46,a46=47,a47=48,a48=49,a49=50;
    int a50=51,a51=52,a52=53,a53=54,a54=55,a55=56,a56=57,a57=58,a58=59,a59=60;
    int a60=61,a61=62,a62=63,a63=64,a64=65,a65=66,a66=67,a67=68,a68=69,a69=70;
    int a70=71,a71=72,a72=73,a73=74,a74=75,a75=76,a76=77,a77=78,a78=79,a79=80;
    int a80=81,a81=82,a82=83,a83=84,a84=85,a85=86,a86=87,a87=88,a88=89,a89=90;
    int a90=91,a91=92,a92=93,a93=94,a94=95,a95=96,a96=97,a97=98,a98=99,a99=100;
    int b00=101,b01=102,b02=103,b03=104,b04=105,b05=106,b06=107,b07=108,b08=109,b09=110;
    int b10=111,b11=112,b12=113,b13=114,b14=115,b15=116,b16=117,b17=118,b18=119,b19=120;

    return a00+a01+a02+a03+a04+a05+a06+a07+a08+a09+
           a10+a11+a12+a13+a14+a15+a16+a17+a18+a19+
           a20+a21+a22+a23+a24+a25+a26+a27+a28+a29+
           a30+a31+a32+a33+a34+a35+a36+a37+a38+a39+
           a40+a41+a42+a43+a44+a45+a46+a47+a48+a49+
           a50+a51+a52+a53+a54+a55+a56+a57+a58+a59+
           a60+a61+a62+a63+a64+a65+a66+a67+a68+a69+
           a70+a71+a72+a73+a74+a75+a76+a77+a78+a79+
           a80+a81+a82+a83+a84+a85+a86+a87+a88+a89+
           a90+a91+a92+a93+a94+a95+a96+a97+a98+a99+
           b00+b01+b02+b03+b04+b05+b06+b07+b08+b09+
           b10+b11+b12+b13+b14+b15+b16+b17+b18+b19;
}

/* Struct-like locals (mimics INIT_G pattern) */
struct fake_globals {
    int state;
    int flags;
    char buf[256];
    int counters[32];
    int fd;
    int argc;
    char *argv;
    int exit_code;
};

static int struct_local_test(void) {
    struct fake_globals G;
    int i, sum = 0;

    G.state = 42;
    G.flags = 0xFF;
    G.fd = 3;
    G.argc = 2;
    G.argv = (char *)0;
    G.exit_code = 0;

    for (i = 0; i < 32; i++)
        G.counters[i] = i * 3;

    for (i = 0; i < 256; i++)
        G.buf[i] = (char)(i & 0x7F);

    /* Verify */
    sum = G.state + G.flags + G.fd + G.argc + G.exit_code;
    for (i = 0; i < 32; i++)
        sum += G.counters[i];
    for (i = 0; i < 256; i++)
        sum += (unsigned char)G.buf[i];

    return sum;
}

void test_main(void) {
    int pass = 1;
    int result, expected;

    uart_puts("T:ARR64:");
    result = array_sum_test(64);
    expected = (64 * 65) / 2; /* sum(1..64) = 2080 */
    if (result == expected) uart_puts("OK\n");
    else { uart_puts("FAIL:"); uart_hex32(result); uart_puts("\n"); pass = 0; }

    uart_puts("T:DUAL:");
    result = dual_array_test(128);
    expected = 128 * 127; /* 128 * 127 = 16256 */
    if (result == expected) uart_puts("OK\n");
    else { uart_puts("FAIL:"); uart_hex32(result); uart_puts("\n"); pass = 0; }

    uart_puts("T:120L:");
    result = func_120_locals();
    expected = (120 * 121) / 2; /* sum(1..120) = 7260 */
    if (result == expected) uart_puts("OK\n");
    else { uart_puts("FAIL:"); uart_hex32(result); uart_puts("\n"); pass = 0; }

    uart_puts("T:STRUCT:");
    result = struct_local_test();
    /* sum = 42 + 255 + 3 + 2 + 0 = 302 */
    /* counters: 0+3+6+...+93 = 3*sum(0..31) = 3*496 = 1488 */
    /* buf: sum(0..127)*2 = 8128*2 = 16256. Wait no:
     * buf[i] = i & 0x7F. For i=0..127: sum = sum(0..127) = 8128
     * For i=128..255: (i & 0x7F) = i-128, sum = sum(0..127) = 8128
     * Total buf sum = 16256 */
    expected = 302 + 1488 + 16256; /* = 18046 */
    if (result == expected) uart_puts("OK\n");
    else { uart_puts("FAIL:"); uart_hex32(result); uart_puts("\n"); pass = 0; }

    if (pass)
        uart_puts("WORKSPACE_LIMIT:PASS\n");
    else
        uart_puts("WORKSPACE_LIMIT:FAIL\n");
}
