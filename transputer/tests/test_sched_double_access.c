/* Needs: fpu
 * The scheduler takes functions that load or store a double through a
 * pseudo-register pair (`ldlp r; fpldnldb`, `fpstnldb`) or convert through
 * one of its words (`fpi32tor64`, `fpstnli32`): both words of the pair are
 * read by the access, so no store to either is dropped or carried past it.
 * Expected: Swords-then-load:P store-then-words:P negate:P abs-bits:P int-convert:P unsigned-convert:P wide-convert:P to-int:P mixed-carry:P compare:P select:P call-between:P stacked:P loop:P copy:P union-return:P float-double:P index:P pending-int:P struct:P \nSCHED-DOUBLE PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)
typedef union { double d; unsigned w[2]; long long ll; } bits;
typedef struct { int tag; double v; int n; } rec;
static int good = 1;
static double gd = 2.5;
static int gi = 7;
static void put(const char *s) { while (*s) UART = (unsigned char)*s++; }
static void check(const char *s, int ok) { put(s); put(ok ? ":P " : ":F "); if (!ok) good = 0; }

static double words_then_load(unsigned lo, unsigned hi, double k)
{ bits b; b.w[0] = lo; b.w[1] = hi; return b.d * k + b.d; }
static unsigned store_then_words(double a, double b, unsigned *lo)
{ bits r; r.d = a * b + a; *lo = r.w[0]; return r.w[1]; }
static double negate(double a, double b) { return -(a * b) - -a; }
static unsigned abs_bits(double a, double b)
{ bits r; r.d = a - b; r.w[1] &= 0x7fffffffu; return r.d == b - a ? r.w[1] : 0; }
static double int_convert(int i, int j, double k) { return i * k + (double)(i + j) / 4 + j; }
static double unsigned_convert(unsigned u, double k) { return u * k + (double)(u >> 1); }
static double wide_convert(long long v, double k) { return (double)v * k + (double)(v >> 3); }
static int to_int(double a, double b, int n)
{ int x = (int)(a * b); int y = (int)(a / b); return x * n + y + (int)(a + n); }
static int mixed_carry(double a, int i, int j, double b)
{ int s = i * 3 + j; int t = (a * b > 6) ? s + i : s - j; return t * (int)(a + b) + s; }
static int compare(double a, double b, int i)
{ return (a < b) + 2 * (a * 2 > b) + 4 * (a == b) + 8 * (i > (int)a) + 16 * ((double)i >= b); }
static double select(double a, double b, int c) { double t = c ? a * b : a + b; return c > 1 ? t - a : t + b; }
static double twice(double x) { gd = gd + 1; return x * 2; }
static double call_between(double a, double b)
{ double t = a * b; double u = twice(t) + gd; return t + u + twice(a) + gd; }
static double stacked(double a, double b, double c, int i, double d) { return a + b * 2 + c * 3 + i + d * 5; }
static double loop(const double *v, int n)
{ double s = 0, p = 1; int i; for (i = 0; i < n; i++) { s = s + v[i] * (i + 1); p = p * (v[i] > 2 ? 2 : 1); } return s + p; }
static void copy(double *d, const double *s, int n) { int i; for (i = 0; i < n; i++) d[n - 1 - i] = s[i]; }
static bits union_return(double a, int k) { bits b; b.d = a * k; b.w[0] ^= (unsigned)k; return b; }
static double float_double(float f, double d, int i) { float g = f * i; return g + d * f + (float)(d * i); }
static double index(const double *v, const int *ix, int n) { return v[ix[n]] * v[ix[n - 1]] + v[n] + ix[0]; }
static int pending_int(int a, double x, int b, double y, int c)
{ return a * 100 + (int)(x * y) * 10 + b + (x > y ? c : -c) * 1000 + (int)(y - x) + gi; }
static double structs(rec *r, const rec *q)
{ r->v = q->v * q->n + r->tag; r->n = (int)(r->v / 2) + q->tag; return r->v + r->n; }

void test_main(void)
{
    bits b, c;
    double v[6], w[6];
    int ix[4];
    rec r, q;
    unsigned lo;
    int i;

    b.d = 1.5;
    check("words-then-load", words_then_load(b.w[0], b.w[1], 4) == 7.5);
    c.d = 1.25 * 3 + 1.25;
    check("store-then-words", store_then_words(1.25, 3, &lo) == c.w[1] && lo == c.w[0]);
    check("negate", negate(3, 0.5) == -1.5 + 3 && negate(-2, 2) == 4 - 2);
    c.d = 2.75;
    check("abs-bits", abs_bits(1.25, 4) == c.w[1] && abs_bits(4, 1.25) == 0);
    check("int-convert", int_convert(6, -2, 0.5) == 3 + 1 - 2);
    check("unsigned-convert", unsigned_convert(0xc0000000u, 0.5) == 1610612736.0 + 1610612736.0);
    check("wide-convert", wide_convert(1LL << 40, 0.25) == 274877906944.0 + 137438953472.0);
    check("to-int", to_int(7.5, 2, 3) == 15 * 3 + 3 + 10);
    check("mixed-carry", mixed_carry(2.5, 4, 5, 3) == (17 + 4) * 5 + 17 &&
          mixed_carry(1, 4, 5, 3) == (17 - 5) * 4 + 17);
    check("compare", compare(1.5, 3, 2) == 1 + 0 + 0 + 8 + 0 && compare(3, 3, 3) == 0 + 2 + 4 + 0 + 16);
    check("select", select(3, 4, 2) == 9 && select(3, 4, 0) == 11 && select(3, 4, 1) == 16);
    gd = 2.5;
    check("call-between", call_between(1.5, 2) == 3 + (6 + 3.5) + 3 + 4.5 && gd == 4.5);
    check("stacked", stacked(1, 2, 3, 4, 0.5) == 1 + 4 + 9 + 4 + 2.5);
    for (i = 0; i < 6; i++) v[i] = i * 0.5 + 1;
    check("loop", loop(v, 6) == 1 + 3 + 6 + 10 + 15 + 21 + 8);
    copy(w, v, 6);
    check("copy", w[0] == 3.5 && w[5] == 1 && w[2] == 2.5 && v[0] == 1);
    c.d = 7.5; c.w[0] ^= 3u;
    b = union_return(2.5, 3);
    check("union-return", b.w[0] == c.w[0] && b.w[1] == c.w[1]);
    check("float-double", float_double(1.5f, 2, 3) == 4.5 + 3 + 6);
    ix[0] = 5; ix[1] = 2; ix[2] = 4; ix[3] = 0;
    check("index", index(v, ix, 2) == 3 * 2 + 2 + 5);
    check("pending-int", pending_int(1, 2.5, 3, 2, 4) == 100 + 50 + 3 + 4000 + 0 + 7 &&
          pending_int(1, 2, 3, 4.5, 4) == 100 + 90 + 3 - 4000 + 2 + 7);
    r.tag = 2; r.v = 0; r.n = 0; q.tag = 5; q.v = 1.5; q.n = 4;
    check("struct", structs(&r, &q) == 8 + 9 && r.v == 8 && r.n == 9 && q.v == 1.5);
    put(good ? "\nSCHED-DOUBLE PASS\n" : "\nSCHED-DOUBLE FAIL\n");
}
