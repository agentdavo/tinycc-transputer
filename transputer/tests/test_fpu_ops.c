/*
 * Needs: fpu
 * Expected: SFPUOPS-OK\nH
 *
 * test_fpu_ops.c - the T800 FPU and error-flag builtins: rounding to an
 * integer value in each mode (__fpint, __fpintrp/rm/rz), the IEEE
 * remainder (__fprem: one instruction on the T9000, the fpremfirst/
 * fpremstep loop on the T800), the FP error flag (__fpseterr, __fpclrerr,
 * __fptesterr, __fpchkerr), HaltOnError (__testhalterr), __testpranal and
 * __stoperr.  Expected values come from the host (Python's exact
 * arithmetic); every comparison is on the bits.
 *
 * After each directed rounding, 1/10, 2/3 and -2/3 must still come out
 * round-to-nearest: on the T9000 the rounding modes are sticky and the
 * builtin has to put the mode back, and the three tell any of the other
 * modes apart from it.
 *
 * Runs on -mt9000, -mt805 and -mt800; the T425 and T414 have no FPU.
 */
#include <conc.h>

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void uh(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        up("0123456789abcdef"[(v >> i) & 15]);
}

static int bad;
static void fail(const char *what, int i, unsigned long long got,
                 unsigned long long want)
{
    bad++;
    us(what); up('['); uh((unsigned)i); us("] got=");
    uh((unsigned)(got >> 32)); uh((unsigned)got);
    us(" want="); uh((unsigned)(want >> 32)); uh((unsigned)want); up('\n');
}

static unsigned long long d2b(double d)
{
    union { double d; unsigned long long u; } v;
    v.d = d;
    return v.u;
}
static unsigned f2b(float f)
{
    union { float f; unsigned u; } v;
    v.f = f;
    return v.u;
}

/* rint, ceil, floor, trunc of each input (Python, ties to even) */
static const double rd_in[] = { 2.5, -2.5, 1.5, -1.5, 0.5, -0.5, -0.4, 0.4, 3.7, -3.7, 10000000000.5, 4503599627370495.5, 1e+30, 0.0, -0.0 };
static const unsigned long long rd_want[][4] = {
    { 0x4000000000000000ULL, 0x4008000000000000ULL, 0x4000000000000000ULL, 0x4000000000000000ULL },  /* 2.5 */
    { 0xC000000000000000ULL, 0xC000000000000000ULL, 0xC008000000000000ULL, 0xC000000000000000ULL },  /* -2.5 */
    { 0x4000000000000000ULL, 0x4000000000000000ULL, 0x3FF0000000000000ULL, 0x3FF0000000000000ULL },  /* 1.5 */
    { 0xC000000000000000ULL, 0xBFF0000000000000ULL, 0xC000000000000000ULL, 0xBFF0000000000000ULL },  /* -1.5 */
    { 0x0000000000000000ULL, 0x3FF0000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL },  /* 0.5 */
    { 0x8000000000000000ULL, 0x8000000000000000ULL, 0xBFF0000000000000ULL, 0x8000000000000000ULL },  /* -0.5 */
    { 0x8000000000000000ULL, 0x8000000000000000ULL, 0xBFF0000000000000ULL, 0x8000000000000000ULL },  /* -0.4 */
    { 0x0000000000000000ULL, 0x3FF0000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL },  /* 0.4 */
    { 0x4010000000000000ULL, 0x4010000000000000ULL, 0x4008000000000000ULL, 0x4008000000000000ULL },  /* 3.7 */
    { 0xC010000000000000ULL, 0xC008000000000000ULL, 0xC010000000000000ULL, 0xC008000000000000ULL },  /* -3.7 */
    { 0x4202A05F20000000ULL, 0x4202A05F20080000ULL, 0x4202A05F20000000ULL, 0x4202A05F20000000ULL },  /* 10000000000.5 */
    { 0x4330000000000000ULL, 0x4330000000000000ULL, 0x432FFFFFFFFFFFFEULL, 0x432FFFFFFFFFFFFEULL },  /* 4503599627370495.5 */
    { 0x46293E5939A08CEAULL, 0x46293E5939A08CEAULL, 0x46293E5939A08CEAULL, 0x46293E5939A08CEAULL },  /* 1e+30 */
    { 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL },  /* 0.0 */
    { 0x8000000000000000ULL, 0x8000000000000000ULL, 0x8000000000000000ULL, 0x8000000000000000ULL },  /* -0.0 */
};
static const float rf_in[] = { 2.5f, -2.5f, 1.5f, -0.5f, -0.4f, 3.7f, -3.7f, 8388607.5f, 1e+30f, -0.0f };
static const unsigned rf_want[][4] = {
    { 0x40000000u, 0x40400000u, 0x40000000u, 0x40000000u },  /* 2.5 */
    { 0xC0000000u, 0xC0000000u, 0xC0400000u, 0xC0000000u },  /* -2.5 */
    { 0x40000000u, 0x40000000u, 0x3F800000u, 0x3F800000u },  /* 1.5 */
    { 0x80000000u, 0x80000000u, 0xBF800000u, 0x80000000u },  /* -0.5 */
    { 0x80000000u, 0x80000000u, 0xBF800000u, 0x80000000u },  /* -0.4 */
    { 0x40800000u, 0x40800000u, 0x40400000u, 0x40400000u },  /* 3.7 */
    { 0xC0800000u, 0xC0400000u, 0xC0800000u, 0xC0400000u },  /* -3.7 */
    { 0x4B000000u, 0x4B000000u, 0x4AFFFFFEu, 0x4AFFFFFEu },  /* 8388607.5 */
    { 0x7149F2CAu, 0x7149F2CAu, 0x7149F2CAu, 0x7149F2CAu },  /* 1e30 */
    { 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u },  /* -0.0 */
};

/* x REM y (Python math.remainder) */
static const double rmd[][2] = {
    { 5.0, 3.0 }, { 7.0, 2.0 }, { 9.0, 2.0 }, { -5.5, 2.0 }, { 1e30, 3.0 },
    { 1e300, 7.0 }, { 123456789.0, 0.001 }, { -0.0, 1.0 }, { 10.0, -3.0 },
    { 6.0, 3.0 }, { -6.0, 3.0 },
};
static const unsigned long long rmd_want[] = {
    0xBFF0000000000000ULL, 0xBFF0000000000000ULL, 0x3FF0000000000000ULL,
    0x3FE0000000000000ULL, 0x3FF0000000000000ULL, 0x3FF0000000000000ULL,
    0xBE2613673F000000ULL, 0x8000000000000000ULL, 0x3FF0000000000000ULL,
    0x0000000000000000ULL, 0x8000000000000000ULL,
};
static const float rmf[][2] = {
    { 10.0f, 3.0f }, { 1e20f, 7.0f }, { -7.5f, 2.0f }, { 16777216.0f, 3.0f },
    { 3e38f, 0.1f },
};
static const unsigned rmf_want[] = {
    0x3F800000u, 0x40400000u, 0x3F000000u, 0x3F800000u, 0xBCA728C8u,
};

static volatile double one = 1.0, two = 2.0, three = 3.0, ten = 10.0, zero = 0.0;
static volatile double inf_d;

/* 1/10, 2/3 and -2/3 must round to nearest */
static void check_nearest(const char *after, int i)
{
    if (d2b(one / ten) != 0x3FB999999999999AULL)
        fail(after, i, d2b(one / ten), 0x3FB999999999999AULL);
    if (d2b(two / three) != 0x3FE5555555555555ULL)
        fail(after, i, d2b(two / three), 0x3FE5555555555555ULL);
    if (d2b(-two / three) != 0xBFE5555555555555ULL)
        fail(after, i, d2b(-two / three), 0xBFE5555555555555ULL);
}

static volatile int passed_stoperr;
static int stopper_ws[256];             /* never reused: the process stays */
static void stopper(void)
{
    __seterr();
    __stoperr();                        /* the Error flag is set: stops here */
    passed_stoperr = 1;
    PStop();
}

int test_main(void)
{
    int i;
    int clean, after;

    /* ---- rounding to an integer value ---- */
    for (i = 0; i < (int)(sizeof rd_in / sizeof rd_in[0]); i++) {
        volatile double x = rd_in[i];
        unsigned long long r;
        if ((r = d2b(__fpint(x))) != rd_want[i][0])   fail("fpint d", i, r, rd_want[i][0]);
        if ((r = d2b(__fpintrp(x))) != rd_want[i][1]) fail("fpintrp d", i, r, rd_want[i][1]);
        check_nearest("after rp", i);
        if ((r = d2b(__fpintrm(x))) != rd_want[i][2]) fail("fpintrm d", i, r, rd_want[i][2]);
        check_nearest("after rm", i);
        if ((r = d2b(__fpintrz(x))) != rd_want[i][3]) fail("fpintrz d", i, r, rd_want[i][3]);
        check_nearest("after rz", i);
    }
    for (i = 0; i < (int)(sizeof rf_in / sizeof rf_in[0]); i++) {
        volatile float x = rf_in[i];
        unsigned r;
        if ((r = f2b(__fpint(x))) != rf_want[i][0])   fail("fpint f", i, r, rf_want[i][0]);
        if ((r = f2b(__fpintrp(x))) != rf_want[i][1]) fail("fpintrp f", i, r, rf_want[i][1]);
        if ((r = f2b(__fpintrm(x))) != rf_want[i][2]) fail("fpintrm f", i, r, rf_want[i][2]);
        if ((r = f2b(__fpintrz(x))) != rf_want[i][3]) fail("fpintrz f", i, r, rf_want[i][3]);
    }
    if (sizeof(__fpint(1.5f)) != 4 || sizeof(__fpint(1.5)) != 8 || sizeof(__fpint(3)) != 8)
        fail("fpint type", 0, sizeof(__fpint(1.5f)), 4);

    /* ---- remainder ---- */
    for (i = 0; i < (int)(sizeof rmd / sizeof rmd[0]); i++) {
        volatile double x = rmd[i][0], y = rmd[i][1];
        unsigned long long r = d2b(__fprem(x, y));
        if (r != rmd_want[i])
            fail("fprem d", i, r, rmd_want[i]);
    }
    for (i = 0; i < (int)(sizeof rmf / sizeof rmf[0]); i++) {
        volatile float x = rmf[i][0], y = rmf[i][1];
        unsigned r = f2b(__fprem(x, y));
        if (r != rmf_want[i])
            fail("fprem f", i, r, rmf_want[i]);
    }
    if (sizeof(__fprem(1.0f, 2.0f)) != 4 || sizeof(__fprem(1.0f, 2.0)) != 8)
        fail("fprem type", 0, sizeof(__fprem(1.0f, 2.0f)), 4);
    check_nearest("after fprem", 0);

    /* ---- the FP error flag ---- */
    __fpclrerr();
    if (!__fptesterr())
        fail("fptesterr clear", 0, 0, 1);
    __fpseterr();
    if (__fptesterr())
        fail("fptesterr after fpseterr", 0, 1, 0);
    if (!__fptesterr())
        fail("fptesterr clears", 0, 0, 1);
    inf_d = one / zero;                 /* division by zero sets it */
    if (__fptesterr())
        fail("fptesterr after 1/0", 0, 1, 0);
    (void)__fpint(inf_d);               /* fpint of an infinity sets it */
    if (__fptesterr())
        fail("fptesterr after fpint(inf)", 0, 1, 0);
    __fpseterr();
    __fpclrerr();
    if (!__fptesterr())
        fail("fpclrerr", 0, 0, 1);

    /* fpchkerr moves the FP error flag into the Error flag */
    (void)__testerr();
    clean = __testerr();                /* what testerr says when clear */
    __fpclrerr();
    __fpchkerr();
    if (__testerr() != clean)
        fail("fpchkerr, no FP error", 0, 1, 0);
    __fpseterr();
    __fpchkerr();
    after = __testerr();
    if (after == clean)
        fail("fpchkerr, FP error", 0, (unsigned)after, (unsigned)!clean);
    __fpclrerr();
    (void)__testerr();

    /* ---- HaltOnError, analyse ---- */
    __clrhalterr();
    if (__testhalterr())
        fail("testhalterr clear", 0, 1, 0);
    __sethalterr();
    if (!__testhalterr())
        fail("testhalterr set", 0, 0, 1);
    __clrhalterr();
    if (__testpranal())
        fail("testpranal", 0, 1, 0);    /* booted from reset */

    /* ---- stoperr: carries on while Error is clear, stops when set ---- */
    (void)__testerr();
    __stoperr();
    PRun(PSetup(stopper_ws, sizeof stopper_ws, stopper) | PRI_LOW);
    ProcWait(5);
    if (passed_stoperr)
        fail("stoperr", 0, 1, 0);
    (void)__testerr();

    if (bad) {
        us("FPUOPS: FAIL ");
        uh((unsigned)bad);
        up('\n');
    } else {
        us("FPUOPS-OK\n");
    }
    return bad;
}
