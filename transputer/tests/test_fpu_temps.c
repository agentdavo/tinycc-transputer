/* T9 FPU architectural Temps T0-T7 reached from C.
 * Target: -mt9000
 *
 * Covers every builtin added for the Temps family and, at the end, checks a
 * full mat4 x vec4 transform driven through f4ldm/tld/fmvmul4 against the
 * same arithmetic written in plain C.  The plain-C side is the oracle: if
 * the two disagree the Temps path is wrong, not the other way round.
 *
 * Needs the real FPU RTL (ip/fpu/rtl/t9_fpu_stage4.sv).  ip/cpu/rtl/
 * t9_fpu_model.sv - the lightweight model the default netload sim links -
 * does not implement the Temps pool at all, so this test cannot run there.
 *
 *   tcc -mt9000 -nostdlib -nostdinc test_boot.S main_wrapper.c test_fpu_temps.c
 *
 * Prints one line per check and "TEMPS OK"/"TEMPS FAIL <n>" at the end.
 */

#define UART_ADDR 0x10000000

static void uart_putc(char c) { *(volatile int *)UART_ADDR = c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static const char hexdigits[] = "0123456789abcdef";
static void uart_puthex(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc(hexdigits[(v >> i) & 0xf]);
}

static unsigned int fbits(float f) {
    union { float f; unsigned int u; } u;
    u.f = f;
    return u.u;
}

static int failures;
static int xfails;

static void check(const char *name, float got, float want)
{
    /* Exact bit compare: every value here is a small dyadic rational, so
     * both paths must produce the identical REAL32 pattern.  A tolerance
     * would hide exactly the lane-ordering bugs this test exists to find. */
    uart_puts(name);
    uart_puts(" got=");
    uart_puthex(fbits(got));
    uart_puts(" want=");
    uart_puthex(fbits(want));
    if (fbits(got) != fbits(want)) {
        failures++;
        uart_puts(" FAIL\n");
    } else {
        uart_puts(" ok\n");
    }
}

/* f2ldm/f4ldm deposit REAL32_V2: data[31:0] = [addr], data[63:32] = [addr+4].
 * fdot4/fmvmul4 read the row from T[i] (lo,hi) and T[i+1] (lo,hi) against
 * FA (lo,hi) and FB (lo,hi), so a 4-word fill at &m[0] paired with a 4-word
 * fill at &v[0] computes m[0]*v[0] + m[1]*v[1] + m[2]*v[2] + m[3]*v[3]. */

static float row[4]  = { 1.0f,  2.0f,  4.0f,  8.0f };
static float vert[4] = { 0.5f, 0.25f, 0.125f, 0.0625f };
static float spill[2];

static float mat[16];
static float src[4];
static float ref[4];
static float out[4];

int main(void)
{
    int i, j;
    float t;

    failures = 0;

    /* --- tst / tld round-trip through T3 --------------------------- */
    __tst(3.5f, 3);
    check("tld(T3)", __tld(3), 3.5f);

    /* --- f2ldm fills a Temp from memory; f2stm spills it back ------- */
    spill[0] = 0.0f;
    spill[1] = 0.0f;
    __f2ldm(&row[0], 5);          /* T5 = { 1.0, 2.0 } as REAL32_V2 */
    __f2stm(&spill[0], 5);
    check("f2stm.lo", spill[0], row[0]);
    check("f2stm.hi", spill[1], row[1]);

    /* --- f2ldma is the async form; the next Temps op fences on it ---- */
    spill[0] = 0.0f;
    spill[1] = 0.0f;
    __f2ldma(&row[2], 5);         /* T5 = { 4.0, 8.0 } */
    __f2stm(&spill[0], 5);
    check("f2ldma.lo", spill[0], row[2]);
    check("f2ldma.hi", spill[1], row[3]);

    /* --- f4ldm fills a Temp pair, and fmvmul4 dots it against FA/FB -- */
    __f4ldm(&row[0],  0);         /* T0 = {1,2}   T1 = {4,8}   */
    __f4ldm(&vert[0], 6);         /* T6 = {.5,.25} T7 = {.125,.0625} */
    t = __fmvmul4(0, 6);
    check("fmvmul4", t,
          row[0]*vert[0] + row[1]*vert[1] + row[2]*vert[2] + row[3]*vert[3]);

    /* --- tclr marks a Temp empty; a following tst refills it --------- */
    __tclr(3);
    __tst(-1.25f, 3);
    check("tclr+tst", __tld(3), -1.25f);

    /* --- fpoly3: Horner with c0,c1,c2 in T[base..base+2] -------------
     *
     * This was an xfail against a real RTL defect: t9_fpu_stage4.sv documented
     * y = ((c2*x + c1)*x + c0) but ran three steps - MUL(c2,x), FMAD(y,x,c1),
     * FMAD(y,x,c0) - i.e. c2*x^3 + c1*x + c0, because step 1 used FMAD against
     * x where Horner wants a plain add of c1.  It returned 2.375 for the vector
     * below where the spec says 2.75.  Fixed 2026-08: Horner on a quadratic is
     * exactly two FMADs, so the fix also dropped a term and FPOLY3 went 16 -> 10
     * cycles.  Now a normal check. */
    __tst(1.0f, 0);               /* c0 */
    __tst(2.0f, 1);               /* c1 */
    __tst(3.0f, 2);               /* c2 */
    t = __fpoly3(0.5f, 0);
    check("fpoly3", t, 1.0f + 0.5f*(2.0f + 0.5f*3.0f));   /* 2.75 */

    /* --- full mat4 x vec4, Temps vs plain C ------------------------- */
    for (i = 0; i < 16; i++)
        mat[i] = (float)((i * 3) % 7) * 0.5f - 1.5f;
    for (i = 0; i < 4; i++)
        src[i] = (float)((i * 5) % 9) * 0.25f - 1.0f;

    for (i = 0; i < 4; i++) {
        float acc = 0.0f;
        for (j = 0; j < 4; j++)
            acc = acc + mat[i*4 + j] * src[j];
        ref[i] = acc;
    }

    /* Rows 0..2 stay resident in T0..T5 while T6,T7 hold the vertex;
     * row 3 needs T0,T1 back, so it gets its own pass. */
    __f4ldm(mat + 0, 0);
    __f4ldm(mat + 4, 2);
    __f4ldm(mat + 8, 4);
    __f4ldm(src + 0, 6);
    __fmvmul4st(0, 6, out + 0);
    __fmvmul4st(2, 6, out + 1);
    __fmvmul4st(4, 6, out + 2);
    __f4ldm(mat + 12, 0);
    __fmvmul4st(0, 6, out + 3);

    check("mvmul4.x", out[0], ref[0]);
    check("mvmul4.y", out[1], ref[1]);
    check("mvmul4.z", out[2], ref[2]);
    check("mvmul4.w", out[3], ref[3]);

    if (xfails) {
        uart_puts("TEMPS XFAIL ");
        uart_puthex((unsigned int)xfails);
        uart_putc('\n');
    }
    if (failures) {
        uart_puts("TEMPS FAIL ");
        uart_puthex((unsigned int)failures);
        uart_putc('\n');
    } else {
        uart_puts("TEMPS OK\n");
    }
    return failures;
}
