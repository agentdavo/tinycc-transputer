/* Return A=low, B=high from computed pairs and aligned memory.  Packed
 * accesses, source addressing and cleanup-preserved bits remain correct.
 * Expected: Sarithmetic:P forward:P pending:P indirect:P conditional:P early:P conversion:P long-double:P distinct-halves:P narrowing:P packed-fallback:P volatile-read:P cleanup:P wide-epilog:P computed-minus-zero:P relay-minus-zero:P relay-subnormal:P computed-infinity:P relay-nan-payload:P memory-local:P memory-pointer:P memory-indexed:P memory-choice:P memory-postinc:P memory-payload:P memory-packed-local:P memory-wide-local:P volatile-pointer:P memory-variadic:P \nDOUBLE-RETURN PASS\nH
 */
#include "test_double_return_pair.h"
#define UART (*(volatile unsigned *)0x10000000u)
typedef union { double d; unsigned w[2]; } bits;
static int good = 1, cleanups;
static volatile double source = 3.25, cleanup_sink;
struct __attribute__((packed)) packed { char tag; double d; };
static void put(const char *s) { while (*s) UART = (unsigned char)*s++; }
static void check(const char *s, int ok) { put(s);put(ok?":P ":":F ");if(!ok)good=0; }
static double arithmetic(double a, double b, double c) { return (a+b)*c; }
static double relay(double x) { return tp_return_identity(x); }
static double forward(double x, double y) { return tp_return_mix(x,y); }
static double pending(double x) { return tp_return_mix(x,1.0)+tp_return_mix(x+1.0,4.0); }
static double choose(int flag, double x)
{ return flag ? tp_return_mix(x,1.0) : (x+1.0)*3.0; }
static double early(double x)
{ if(x<0)return -x;if(x==0)return __fpsqrt(4.0);return x*x; }
static double converted(int x) { return (double)x; }
static double narrowed(double x) { return (float)x; }
static long double extended(long double x) { return x*2.0L+1.0L; }
static double plus_ulp(double x) { return x+0x1p-52; }
static double scaled(double x) { return x*2.0; }
static double packed_read(struct packed *p) { return p->d; }
static double volatile_read(void) { return source; }
static double local_read(double x) { double d=tp_return_mix(x,1.0);return d; }
static double indexed_read(const double *p,int i) { return p[i]; }
static double choice_read(int c,const double *a,const double *b)
{ return *(c?a:b); }
static double postinc_read(const double **p) { return *(*p)++; }
static double volatile_pointer(const volatile double *p) { return *p; }
static double variadic_read(double x,int tag,...) { if(tag)return x;return source; }
static double packed_local(double x)
{ struct packed p;p.tag=7;p.d=x;return p.d; }
static double wide_local(double x)
{ volatile char space[17000];double d=tp_return_identity(x);space[16999]=3;
  if(space[16999]==3)return d;return 0; }
static void cleanup_token(int *token)
{ cleanup_sink=tp_return_mix(7.0,3.0);cleanups+=*token; }
static double cleanup_return(double x)
{ int token __attribute__((cleanup(cleanup_token)))=1;return x*x+1.0; }
static double large_frame(double x, int n)
{ volatile char space[17000];space[0]=n;space[16999]=n+1;
  if(n)return x*2.0+space[0];return x*3.0+space[16999]; }
static double (*volatile indirect)(double,double)=tp_return_mix;

void test_main(void)
{
    bits in,out;
    struct packed p;
    double a[3];const double *cursor=a;
    check("arithmetic",arithmetic(2.0,3.0,4.0)==20.0);
    check("forward",forward(2.0,3.0)==7.0);
    check("pending",pending(2.0)==15.0);
    check("indirect",indirect(2.0,3.0)==7.0);
    check("conditional",choose(1,2.0)==5.0 && choose(0,2.0)==9.0);
    check("early",early(-3.0)==3.0 && early(0.0)==2.0 && early(3.0)==9.0);
    check("conversion",converted(-33)==-33.0);
    check("long-double",extended(3.0L)==7.0L);
    in.w[0]=1;in.w[1]=0x3ff00000u;out.d=plus_ulp(in.d);
    check("distinct-halves",out.w[0]==2 && out.w[1]==0x3ff00000u);
    check("narrowing",narrowed(in.d)==1.0);
    p.tag=7;p.d=3.25;
    check("packed-fallback",packed_read(&p)==3.25);
    check("volatile-read",volatile_read()==3.25);
    check("cleanup",cleanup_return(3.0)==10.0 && cleanups==1 && cleanup_sink==17.0);
    check("wide-epilog",large_frame(2.0,3)==7.0 && large_frame(2.0,0)==7.0);
    in.w[0]=0;in.w[1]=0x80000000u;out.d=scaled(in.d);
    check("computed-minus-zero",out.w[0]==0 && out.w[1]==0x80000000u);
    out.d=relay(in.d);
    check("relay-minus-zero",out.w[0]==in.w[0] && out.w[1]==in.w[1]);
    in.w[0]=1;in.w[1]=0;out.d=relay(in.d);
    check("relay-subnormal",out.w[0]==in.w[0] && out.w[1]==in.w[1]);
    in.w[0]=0;in.w[1]=0x7ff00000u;out.d=scaled(in.d);
    check("computed-infinity",out.w[0]==in.w[0] && out.w[1]==in.w[1]);
    in.w[0]=0x12345678u;in.w[1]=0x7ff80000u;out.d=relay(in.d);
    check("relay-nan-payload",out.w[0]==in.w[0] && out.w[1]==in.w[1]);
    a[0]=2.25;a[1]=3.5;a[2]=4.75;
    check("memory-local",local_read(3.0)==7.0);
    check("memory-pointer",tp_return_read(a)==2.25);
    check("memory-indexed",indexed_read(a,2)==4.75);
    check("memory-choice",choice_read(0,a,a+1)==3.5 && choice_read(1,a,a+1)==2.25);
    check("memory-postinc",postinc_read(&cursor)==2.25 && cursor==a+1);
    out.d=tp_return_read(&in.d);
    check("memory-payload",out.w[0]==in.w[0] && out.w[1]==in.w[1]);
    check("memory-packed-local",packed_local(3.25)==3.25);
    check("memory-wide-local",wide_local(3.25)==3.25);
    check("volatile-pointer",volatile_pointer(&source)==3.25);
    check("memory-variadic",variadic_read(3.75,1,8.0,9)==3.75 && variadic_read(3.75,0,8.0,9)==3.25);
    put(good?"\nDOUBLE-RETURN PASS\n":"\nDOUBLE-RETURN FAIL\n");
}
