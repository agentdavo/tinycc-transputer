/* Incoming double parameters are private ABI copies, including the C/stack
 * split pair.  Exercise their storage across calls, mutation and escapes. */
#include <stdarg.h>
#include "test_double_param_storage.h"
#define UART (*(volatile unsigned *)0x10000000u)
typedef union { double d; unsigned w[2]; } bits;
static int good = 1;
static void put(const char *s) { while (*s) UART = (unsigned char)*s++; }
static void check(const char *s, int ok) { put(s);put(ok?":P ":":F ");if(!ok)good=0; }

static double horner(double a, double b, double c) { return a * b + c; }
static double mixed(char tag, double a, short k, double b, int z, double c)
{ return a * b + c + tag + k + z; }
static double identity(double x) { return x; }
static double flip(double x) { ((unsigned char *)&x)[7] ^= 128; return x; }
static void change(double *p) { *p = *p * 2.0 + 1.0; }
static double address(double x, double y) { change(&x); return x + y; }
static double *escaped;
static void change_escaped(void) { *escaped = *escaped + 5.0; }
static double escape(double x) { escaped = &x; change_escaped(); return x; }
static double recursive(double x, int n)
{ double before = x; return n ? recursive(x + 1.0, n - 1) + before : x; }
static double volatile_arg(volatile double x) { x = x + 1.0; return x + x; }
static long double extended_arg(long double x, long double y) { return x * y; }
static double sum12(double a,double b,double c,double d,double e,double f,
                    double g,double h,double i,double j,double k,double l)
{ return a+b+c+d+e+f+g+h+i+j+k+l; }
static double wide_call(double x)
{ return sum12(x,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11) + x; }
static double nested(double x) { return wide_call(x) + horner(x,2.0,3.0); }
static double vararg(double x, int n, ...)
{ va_list ap;double y;va_start(ap,n);y=va_arg(ap,double);va_end(ap);return x+y+n; }
static struct pair make_pair(double x, double y)
{ struct pair p;p.x=x+1;p.y=y+2;p.tag=7;return p; }
static double (*volatile indirect)(double,double,double) = horner;
struct __attribute__((packed)) packed { char tag; double x; };
static double packed_call(double x)
{ char c=3;struct packed p;p.tag=c;p.x=x;return indirect(p.x,2.0,p.tag); }

void test_main(void)
{
    bits in, out;
    double original = 3.0;
    struct pair p;
    check("three-pairs", horner(2.0,3.0,4.0)==10.0);
    check("mixed", mixed(1,2.0,3,4.0,5,6.0)==23.0);
    check("indirect", indirect(2.0,3.0,4.0)==10.0);
    check("address", address(original,2.0)==9.0 && original==3.0);
    check("escape", escape(original)==8.0 && original==3.0);
    check("recursive", recursive(2.0,3)==14.0);
    check("wide-call", wide_call(1.0)==79.0);
    check("nested", nested(2.0)==99.0);
    check("volatile", volatile_arg(3.0)==8.0);
    check("long-double", extended_arg(3.0L,4.0L)==12.0L);
    check("vararg", vararg(2.0,3,4.0)==9.0);
    p=make_pair(3.0,4.0);
    check("sret", p.x==4.0 && p.y==6.0 && p.tag==7);
    check("external-split", tp_external_double(2.0,3.0,5)==14.0);
    p=tp_external_pair(3.0,4.0);
    check("external-sret", p.x==4.0 && p.y==6.0 && p.tag==7);
    check("packed-caller", packed_call(7.0)==17.0);
    check("byte-mutation", flip(original)==-3.0 && original==3.0);
    in.w[0]=0;in.w[1]=0x80000000u;out.d=identity(in.d);
    check("negative-zero", out.w[0]==in.w[0] && out.w[1]==in.w[1]);
    in.w[0]=0;in.w[1]=0x7ff00000u;out.d=identity(in.d);
    check("infinity", out.w[0]==in.w[0] && out.w[1]==in.w[1]);
    in.w[0]=0x12345678u;in.w[1]=0x7ff80000u;out.d=identity(in.d);
    check("nan-payload", out.w[0]==in.w[0] && out.w[1]==in.w[1]);
    put(good?"\nDOUBLE-PARAM PASS\n":"\nDOUBLE-PARAM FAIL\n");
}
