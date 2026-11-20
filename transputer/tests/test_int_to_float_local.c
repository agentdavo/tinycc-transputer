/* Expected: SITOF_OK\nH */
typedef unsigned int u32;
typedef union { float f; u32 u; } F;
typedef union { double d; u32 w[2]; } D;
struct __attribute__((packed)) P { char x; int value; };
volatile int seed = -123456;
volatile unsigned seedu = 0xffffffffu;
static float retf(void) { return 2.0f; }
static double retd(void) { return 4.0; }
static int reti(void) { return -7; }
int test_main(void) {
 volatile int a = seed; volatile unsigned b = seedu; volatile int lo = (-2147483647 - 1), hi = 2147483647;
 struct P p; F f; D d; int ok = 1;
 f.f = (float)lo; ok &= f.u == 0xcf000000u;
 f.f = (float)hi; ok &= f.u == 0x4f000000u;
 d.d = (double)lo; ok &= d.w[1] == 0xc1e00000u && d.w[0] == 0;
 d.d = (double)hi; ok &= d.w[1] == 0x41dfffffu && d.w[0] == 0xffc00000u;
 f.f = (float)a; ok &= f.u == 0xc7f12000u;
 d.d = (double)a; ok &= d.w[1] == 0xc0fe2400u && d.w[0] == 0;
 f.f = (float)b; ok &= f.u == 0x4f800000u;
 d.d = (double)b; ok &= d.w[1] == 0x41efffffu && d.w[0] == 0xffe00000u;
 ok &= retf() + (float)a == -123454.0f;
 ok &= retd() + (double)a == -123452.0;
 ok &= (float)reti() == -7.0f;
 p.value = -42; ok &= (double)p.value == -42.0;
 { const char *s = ok ? "ITOF_OK\n" : "ITOF_FAIL\n"; while (*s) *(volatile unsigned *)0x10000000u = (unsigned char)*s++; }
 return 0;
}
