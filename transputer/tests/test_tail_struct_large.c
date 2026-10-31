/*
 * test_tail_struct_large.c — Test large local buffers + struct pointer args.
 *
 * Tests the 12-bit offset limit (4095 words) in transputer-gen.c's local
 * variable patcher. With a 16KB+ local buffer, the LDLP/LDL/STL offset
 * for variables beyond the buffer will exceed 4095 words and be silently
 * truncated.
 *
 * UART oracle: A1P\nA2P\nB1P\nB2P\nC1P\nC2P\nH
 */
#define UART_DATA  (*(volatile unsigned int *)0x10000000)

static void uart_putc(int c) { UART_DATA = c; }

static void uart_hex8(unsigned int v) {
    int nib = (v >> 4) & 0xF;
    uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    nib = v & 0xF;
    uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
}

static void uart_hex32(unsigned int v) {
    uart_hex8((v >> 24) & 0xFF);
    uart_hex8((v >> 16) & 0xFF);
    uart_hex8((v >>  8) & 0xFF);
    uart_hex8((v >>  0) & 0xFF);
}

struct result {
    unsigned long val1;
    unsigned long val2;
};

/*
 * We don't loop over the buffer — just declare it to consume workspace,
 * touch first/last bytes, then store through the struct pointer.
 * This tests whether the offset patching handles the workspace size correctly.
 */

/* ---- Test A: 1KB local buffer (256 words, well under limit) ---- */
static int test_a_1kb(struct result *out, unsigned long seed)
{
    char buf[1024];
    buf[0] = (char)seed;
    buf[1023] = (char)(seed + 1);
    out->val1 = (unsigned char)buf[0] + (unsigned char)buf[1023];
    out->val2 = seed;
    return 0;
}

/* ---- Test B: 8KB local buffer (~2048 words, approaching limit) ---- */
static int test_b_8kb(struct result *out, unsigned long seed)
{
    char buf[8192];
    buf[0] = (char)seed;
    buf[8191] = (char)(seed + 1);
    out->val1 = (unsigned char)buf[0] + (unsigned char)buf[8191];
    out->val2 = seed;
    return 0;
}

/* ---- Test C: 17KB local buffer (4352 words, exceeds 4095-word limit!) ---- */
static int test_c_17kb(struct result *out, unsigned long seed)
{
    char buf[17408];
    buf[0] = (char)seed;
    buf[17407] = (char)(seed + 1);
    out->val1 = (unsigned char)buf[0] + (unsigned char)buf[17407];
    out->val2 = seed;
    return 0;
}

void test_main(void)
{
    struct result r;

    /* Test A: 1KB buffer */
    r.val1 = 0xDEAD; r.val2 = 0xDEAD;
    test_a_1kb(&r, 42);
    uart_putc('A'); uart_putc('1');
    /* Sum of (42+i)%256 for i=0..1023 */
    if (r.val2 == 42) uart_putc('P'); else { uart_putc('F'); uart_hex32(r.val2); }
    uart_putc('\n');
    uart_putc('A'); uart_putc('2');
    if (r.val1 != 0xDEAD && r.val1 != 0) uart_putc('P'); else { uart_putc('F'); uart_hex32(r.val1); }
    uart_putc('\n');

    /* Test B: 8KB buffer */
    uart_putc('B'); uart_putc('0'); uart_putc('\n');
    r.val1 = 0xDEAD; r.val2 = 0xDEAD;
    test_b_8kb(&r, 99);
    uart_putc('B'); uart_putc('1');
    if (r.val2 == 99) uart_putc('P'); else { uart_putc('F'); uart_hex32(r.val2); }
    uart_putc('\n');
    uart_putc('B'); uart_putc('2');
    if (r.val1 != 0xDEAD && r.val1 != 0) uart_putc('P'); else { uart_putc('F'); uart_hex32(r.val1); }
    uart_putc('\n');

    /* Test C: 17KB buffer (exceeds 4095-word limit) */
    uart_putc('C'); uart_putc('0'); uart_putc('\n');
    r.val1 = 0xDEAD; r.val2 = 0xDEAD;
    test_c_17kb(&r, 77);
    uart_putc('C'); uart_putc('1');
    if (r.val2 == 77) uart_putc('P'); else { uart_putc('F'); uart_hex32(r.val2); }
    uart_putc('\n');
    uart_putc('C'); uart_putc('2');
    if (r.val1 != 0xDEAD && r.val1 != 0) uart_putc('P'); else { uart_putc('F'); uart_hex32(r.val1); }
    uart_putc('\n');

    uart_putc('H');
}
