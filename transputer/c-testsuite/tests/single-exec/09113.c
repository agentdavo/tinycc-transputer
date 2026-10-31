/* A double result is two workspace words, REG_FRET and REG_FRET+1, but the
 * int->float and float->float conversions only spilled REG_FRET.  In
 * `(fa - (float)ib) - (fc - dd)` the left side was parked in REG_FRET+1 as
 * a float, and the right side's (double)fc overwrote it, so the expression
 * ignored its left operand.  Found by fuzz/difftest.sh (seeds 9 and 18).
 * Expected output is gcc's. */
int printf(const char *, ...);
typedef unsigned short u16;

static u16 b = 300;
static int ib = 300, p0 = 7;
static unsigned ub = 4000000000u;
static float fa = -8.0f, fb = 300.0f, fc = 7.0f;
static double dd = 255.0;

static int bits(float f) { union { float f; int i; } v; v.f = f; return v.i; }

int main(void)
{
    double x1 = (fa - (float)ib) - (fc - dd);
    double x2 = (fa - (float)b) - (fc - dd);
    double x3 = (fa * (float)ib) + ((double)fc * dd);
    double x4 = (fa - (float)ub) - (fc - dd);
    double x5 = ((float)ib - fa) / ((double)fc + dd);
    float w1 = (float)(((float)((float)(signed char)0xf8 - (float)b)) - ((float)((float)(u16)p0 - (double)255)));
    float w2 = (-8.0f - (float)b) - ((float)(u16)p0 - 255.0);
    int i1 = (int)((fa - (float)ib) - (fc - dd));

    printf("%d %d %d %d %d\n", (int)x1, (int)x2, (int)x3, (int)(x4 / 1000.0), (int)(x5 * 1000.0));
    printf("%08x %08x %d\n", bits(w1), bits(w2), i1);
    return 0;
}
