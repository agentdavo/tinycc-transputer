/*
 * test_sign_extend.c - narrow-to-word sign extension.
 *
 * -mt800 now uses XWORD (`ldc mask; and; ldc half; xword`) instead of the
 * 6-instruction `and/xor/diff` formula.  XWORD has ACWG preconditions
 * (t4/p.c case 0x3a): bitcount(AReg) == 1 and 2*AReg > BReg, i.e. the value
 * must already be masked into range.  The `and` that supplies that guarantee
 * must therefore NOT be optimised away, including on the paths that reach the
 * helper with an arbitrary int (casts), which is what this test pins down.
 *
 * -mt9000 uses xbword/xsword instead and should be unaffected; run both.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void ud(unsigned v){char b[12];int n=0;if(!v){up('0');return;}while(v){b[n++]=(char)('0'+v%10);v/=10;}while(n)up(b[--n]);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?('0'+n):('a'+n-10));}}

static int bad = 0, ck = 0;
static void eq(const char *w, int i, long g, long e)
{
    ck++;
    if (g != e) {
        bad++;
        us("\nFAIL "); us(w); us("["); ud((unsigned)i);
        us("] got="); uh((unsigned)g); us(" want="); uh((unsigned)e);
    }
}

static signed char  SC[8];
static short        SH[8];
static volatile int sink;

/* loads through a pointer: value is already narrow */
static void t_load(void)
{
    static const int cv[8] = { 0, 1, 0x7F, -1, -2, -128, 0x55, -86 };
    static const int hv[8] = { 0, 1, 0x7FFF, -1, -2, -32768, 0x5555, -21846 };
    int i;
    for (i = 0; i < 8; i++) { SC[i] = (signed char)cv[i]; SH[i] = (short)hv[i]; }
    for (i = 0; i < 8; i++) {
        eq("ldchar",  i, SC[i], cv[i]);
        eq("ldshort", i, SH[i], hv[i]);
    }
}

/* casts: the helper is reached with an ARBITRARY int, so the mask matters.
 * (signed char)0x100 must be 0, not 0x100. */
static void t_cast(void)
{
    static const unsigned wide[10] = {
        0u, 1u, 0x7Fu, 0x80u, 0xFFu, 0x100u, 0x1FFu, 0xFFFFFF80u, 0x12345678u, 0xFFFFFFFFu
    };
    static const int want_c[10] = { 0, 1, 127, -128, -1, 0, -1, -128, 0x78, -1 };
    static const int want_h[10] = { 0, 1, 127, 128, 255, 256, 511, -128, 0x5678, -1 };
    int i;
    for (i = 0; i < 10; i++) {
        sink = (int)wide[i];                 /* defeat constant folding */
        eq("castc", i, (signed char)sink, want_c[i]);
        eq("casth", i, (short)sink,       want_h[i]);
    }
}

/* the boundary values, one at a time, through a variable */
static void t_boundary(void)
{
    volatile int v;
    v = 0x7F;  eq("b", 0, (signed char)v,  127);
    v = 0x80;  eq("b", 1, (signed char)v, -128);
    v = 0x81;  eq("b", 2, (signed char)v, -127);
    v = 0xFF;  eq("b", 3, (signed char)v,   -1);
    v = 0x7FFF; eq("b", 4, (short)v,  32767);
    v = 0x8000; eq("b", 5, (short)v, -32768);
    v = 0x8001; eq("b", 6, (short)v, -32767);
    v = 0xFFFF; eq("b", 7, (short)v,     -1);
}

/* sign extension feeding arithmetic and comparison, where a wrong high half
 * would show up as a wrong branch rather than a wrong value */
static void t_used(void)
{
    volatile int v = 0xFF;
    signed char c = (signed char)v;
    short s;
    eq("use", 0, c < 0, 1);
    eq("use", 1, c + 1, 0);
    eq("use", 2, c * 2, -2);
    eq("use", 3, (int)c >> 1, -1);
    v = 0x8000; s = (short)v;
    eq("use", 4, s < 0, 1);
    eq("use", 5, s + 1, -32767);
    /* unsigned narrow must NOT sign extend */
    { volatile int u = 0xFF; unsigned char uc = (unsigned char)u;
      eq("use", 6, uc, 255); eq("use", 7, uc < 0, 0); }
    { volatile int u = 0xFFFF; unsigned short uh_ = (unsigned short)u;
      eq("use", 8, uh_, 65535); }
}

int test_main(void)
{
    t_load();
    t_cast();
    t_boundary();
    t_used();

    us("\nchecks="); ud((unsigned)ck); us(" failures="); ud((unsigned)bad);
    us(bad ? "\nSEXT: FAIL\n" : "\nSEXT: PASS\n");
    return bad ? 1 : 0;
}
