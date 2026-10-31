/* ?: on doubles whose arms land differently.  A double is two words; a ?:
 * pins its result to the return register class, and with no RC_FRE2 the
 * transputer's RC2_TYPE() sent the high word to an integer register on one
 * arm while gv() loaded the other arm (a constant or an lvalue) as the slot
 * pair r/r+1.  `c ? (double)uc : -128.0` returned the right low word with the
 * wrong high word on the false arm.  Found by fuzz/difftest.sh (seeds 65 and
 * nine more).  Expected output is gcc's. */
int printf(const char *, ...);
typedef unsigned u32;

static unsigned short gh = 50632, gl = 5;
static unsigned char gc = 200;
static double gd = -128.0;
static float gf = 2.5f;
static long long gll = -3;

static u32 hi(double d) { union { double d; u32 u[2]; } v; v.d = d; return v.u[1]; }

static double t1(unsigned short h) { return (double)h < 1024.0 ? (double)gc : -128.0; }
static double t2(unsigned short h) { return (double)h < 1024.0 ? (double)gc : gd; }
static double t3(unsigned short h) { return h < 1024 ? (double)gc : -128.0; }
static double t4(unsigned short h) { return h < 1024 ? -128.0 : (double)gc; }
static double t5(unsigned short h) { return h < 1024 ? (double)gf : (double)gll; }
static double t6(unsigned short h) { double r = h < 1024 ? (double)gc : gd * 2.0; return r + 1.0; }
static double vsel(int n, double a, double b) { return n ? a : b; }

int main(void)
{
    printf("%08x %08x %08x %08x\n", hi(t1(gh)), hi(t1(gl)), hi(t2(gh)), hi(t2(gl)));
    printf("%08x %08x %08x %08x\n", hi(t3(gh)), hi(t3(gl)), hi(t4(gh)), hi(t4(gl)));
    printf("%08x %08x %08x %08x\n", hi(t5(gh)), hi(t5(gl)), hi(t6(gh)), hi(t6(gl)));
    printf("%d %d %d\n", (int)t1(gh), (int)t6(gh), (int)vsel(gl > 3, (double)gc, -128.0));
    return 0;
}
