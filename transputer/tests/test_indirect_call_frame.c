/* A call through a function pointer stores its return address under Wptr
 * before the AJW that enters the callee's frame, so no slot number changes
 * meaning inside the function and the post-pass (scheduler, slot remap,
 * global-address cache) takes functions with indirect calls.
 * Expected: Sno-args:P ints:P stacked:P doubles:P mixed:P wide-return:P struct-return:P member:P nested:P returned:P recursive:P table:P cached-global:P callee-writes:P condition:P pending:P many-locals:P floats:P narrow:P variadic:P reentrant:P vla:P goto:P switch:P pointer-kept:P \nINDIRECT-CALL PASS\nH
 */
#include <stdarg.h>
#define UART (*(volatile unsigned *)0x10000000u)
typedef long long i64;
typedef struct { int a, b, c; } tri;
struct obj { int (*m)(struct obj *, int); int k; };
static int good = 1;
int icf_g1 = 10, icf_g2 = 20, icf_g3 = 30;
static int calls;
static void put(const char *s) { while (*s) UART = (unsigned char)*s++; }
static void check(const char *s, int ok) { put(s); put(ok ? ":P " : ":F "); if (!ok) good = 0; }

static int zero(void) { calls++; return 7; }
static int one(int a) { return a + 1; }
static int two(int a, int b) { return a * 10 + b; }
static int three(int a, int b, int c) { return a * 100 + b * 10 + c; }
static int six(int a, int b, int c, int d, int e, int f)
{ return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f; }
static double dd(double a, double b) { return a * b - a; }
static double mix(int a, double b, char c, i64 d, float e)
{ return a + b + c + (double)(int)d + e; }
static i64 wide(i64 a, int s) { return (a << s) + 1; }
static tri mk(int a, int b) { tri t; t.a = a; t.b = b; t.c = a + b; return t; }
static int meth(struct obj *o, int x) { return o->k * x; }
static int bump(int x) { icf_g1 += x; return icf_g1; }
static float ff(float a, float b) { return a / b; }
static int narrow(signed char a, unsigned char b, short c) { return a + b + c; }
static int sum(int n, ...)
{ va_list ap; int s = 0; va_start(ap, n); while (n--) s += va_arg(ap, int); va_end(ap); return s; }
typedef int (*f1)(int);
static f1 pick(int i) { return i ? one : bump; }
static int fact(int (*self)(void *, int), int n) { return n < 2 ? 1 : n * self((void *)self, n - 1); }
static int fact_tramp(void *self, int n) { return fact((int (*)(void *, int))self, n); }
static int inner(int (*f)(int, int, int, int, int, int), int x)
{ return f(x, x + 1, x + 2, x + 3, x + 4, x + 5); }
static int outer(int (*g)(int (*)(int, int, int, int, int, int), int),
                 int (*f)(int, int, int, int, int, int), int a, int b, int c, int d)
{ return g(f, a) + g(f, b) * 2 + c + d; }

static int t_no_args(int (*f)(void)) { return f() + f() * 2; }
static int t_ints(f1 a, int (*b)(int, int), int (*c)(int, int, int), int x)
{ return a(x) + b(x, x + 1) + c(x, x + 1, x + 2); }
static int t_stacked(int (*f)(int, int, int, int, int, int), int x)
{ return f(1, 2, 3, 4, 5, x) - f(x, 5, 4, 3, 2, 1); }
static double t_doubles(double (*f)(double, double), double a, double b)
{ return f(a, b) + f(b, a) * f(a, a); }
static double t_mixed(double (*f)(int, double, char, i64, float), int a)
{ return f(a, 1.5, 'A', 100000000000LL + a, 0.25f); }
static i64 t_wide(i64 (*f)(i64, int), i64 v) { return f(v, 33) + f(v, 1); }
static int t_struct(tri (*f)(int, int), int x)
{ tri t = f(x, 3); tri u = f(t.c, t.a); return t.a + t.b * 10 + t.c * 100 + u.c * 1000; }
static int t_member(struct obj *o, int n)
{ int s = 0; while (n--) s += o->m(o, n) + o->k; return s; }
static int t_nested(f1 a, int (*b)(int, int), int x) { return b(a(x), a(a(x + 1))); }
static int t_returned(int i, int x) { return pick(i)(x) + pick(!i)(x); }
static int t_table(int (**t)(int), int n, int x)
{ int i, s = 0; for (i = 0; i < n; i++) s = s * 3 + t[i & 1](x + i); return s; }
static int t_cached(f1 f, int x)
{ return f(icf_g2) + icf_g2 * 2 + f(icf_g3) + icf_g2 + icf_g3 + icf_g3 + x; }
static int t_callee_writes(f1 f)
{ int a = icf_g1; int b = f(5); int c = icf_g1; int d = f(1); return a + b * 10 + c * 100 + d * 1000 + icf_g1 * 10000; }
static int t_condition(f1 f, int x) { if (f(x) > 5 && f(x + 1) < 100) return f(x) ? f(1) : f(2); return -1; }
static int t_pending(f1 f, int (*g)(int, int), int a, int b)
{ return a + f(b) * g(a, b) - (b - f(a)) * (g(b, a) + a); }
static int t_many(f1 f, int x)
{
    int a = x, b = a + 1, c = b + 1, d = c + 1, e = d + 1, g = e + 1, h = g + 1, i = h + 1;
    int j = i + 1, k = j + 1, l = k + 1, m = l + 1, n = m + 1, o = n + 1, p = o + 1, q = p + 1;
    int r = f(a) + f(q), s = f(b + p), t = f(c * o);
    return a + b + c + d + e + g + h + i + j + k + l + m + n + o + p + q + r * s - t;
}
static float t_floats(float (*f)(float, float), float a) { return f(a, 4) + f(1, a); }
static int t_narrow(int (*f)(signed char, unsigned char, short), int x)
{ return f((signed char)(x + 200), (unsigned char)(x + 250), (short)(x + 40000)); }
static int t_variadic(int (*f)(int, ...), int x) { return f(5, x, 2, 3, 4, 5) + f(0) + f(1, x); }
static int t_vla(f1 f, int n)
{ int v[n]; int i, s = 0; for (i = 0; i < n; i++) v[i] = f(i); for (i = 0; i < n; i++) s += v[i] * f(s & 3); return s; }
static int t_goto(f1 f, int x)
{
    static void *const lab[] = { &&l0, &&l1, &&l2 };
    int s = 0;
    goto *lab[x % 3];
l0: s += f(x); x++;
l1: s += f(x) * 2; x++;
l2: s += f(x) * 3;
    return s;
}
static int t_switch(f1 f, int x)
{
    switch (x) {
    case 0: return f(1);
    case 1: return f(2) + f(3);
    case 2: return f(f(4));
    case 3: return 3;
    case 4: return f(x) * 4;
    case 5: return f(5) - 5;
    default: return f(-1);
    }
}
static int t_pointer_kept(f1 *pf, int x)
{ int a = (*pf)(x); *pf = one; return a + (*pf)(x) + (*pf)(a); }

void test_main(void)
{
    struct obj o;
    int (*tab[2])(int);
    f1 fp;
    int i, s;

    check("no-args", t_no_args(zero) == 21 && calls == 2);
    check("ints", t_ints(one, two, three, 4) == 5 + 45 + 456);
    check("stacked", t_stacked(six, 9) == (1 + 4 + 9 + 16 + 25 + 54) - (9 + 10 + 12 + 12 + 10 + 6));
    check("doubles", t_doubles(dd, 3, 0.5) == (1.5 - 3) + (1.5 - 0.5) * 6);
    check("mixed", t_mixed(mix, 3) == 3 + 1.5 + 65 + (double)(int)(100000000000LL + 3) + 0.25);
    check("wide-return", t_wide(wide, 3) == (3LL << 33) + 1 + 7);
    check("struct-return", t_struct(mk, 2) == 2 + 30 + 500 + 7000);
    o.m = meth; o.k = 3;
    check("member", t_member(&o, 4) == 3 * (0 + 1 + 2 + 3) + 12);
    check("nested", t_nested(one, two, 4) == 57);
    icf_g1 = 10;
    check("returned", t_returned(1, 4) == 5 + 14 && icf_g1 == 14);
    check("recursive", fact(fact_tramp, 6) == 720);
    tab[0] = one; tab[1] = bump; icf_g1 = 0;
    for (i = 0, s = 0; i < 4; i++) s = s * 3 + (i & 1 ? (icf_g1 += 2 + i) : 3 + i);
    icf_g1 = 0;
    check("table", t_table(tab, 4, 2) == s);
    check("cached-global", t_cached(one, 5) == 21 + 40 + 31 + 20 + 60 + 5);
    icf_g1 = 1;
    check("callee-writes", t_callee_writes(bump) == 1 + 60 + 600 + 7000 + 70000);
    check("condition", t_condition(one, 5) == 2 && t_condition(one, 1) == -1);
    check("pending", t_pending(one, two, 3, 4) == 3 + 5 * 34 - (4 - 4) * (43 + 3));
    check("many-locals", t_many(one, 1) == 136 + 19 * 18 - 43);
    check("floats", t_floats(ff, 2) == 1.0f);
    check("narrow", t_narrow(narrow, 10) == -46 + 4 + (short)40010);
    check("variadic", t_variadic(sum, 6) == 20 + 0 + 6);
    check("reentrant", outer(inner, six, 1, 2, 3, 4) == 91 + 112 * 2 + 7);
    check("vla", t_vla(one, 5) == 47);
    check("goto", t_goto(one, 3) == 4 + 10 + 18 && t_goto(one, 5) == 18);
    for (i = 0, s = 0; i < 8; i++) s = s * 2 + t_switch(one, i);
    check("switch", s == ((((((2 * 2 + 7) * 2 + 6) * 2 + 3) * 2 + 20) * 2 + 1) * 2 + 0) * 2 + 0);
    fp = bump; icf_g1 = 0;
    check("pointer-kept", t_pointer_kept(&fp, 5) == 5 + 6 + 6 && fp == one);
    put(good ? "\nINDIRECT-CALL PASS\n" : "\nINDIRECT-CALL FAIL\n");
}
