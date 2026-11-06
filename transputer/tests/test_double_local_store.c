/* Double arithmetic results may store directly to an unescaped local only
 * when their temporary pair and any high-word copy are dead.
 * Expected: Srecurrence:P two-locals:P quotient:P chained:P assignment-value:P snapshot:P escaped:P volatile:P volatile-cast:P packed:P pending:P goto:P for-step:P branch:P minus-zero:P mixed:P wide:P \nDOUBLE-STORE PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)
typedef union { double d;unsigned w[2]; } bits;
struct __attribute__((packed)) packed { char c;double d; };
static int good=1;
static void put(const char*s){while(*s)UART=(unsigned char)*s++;}
static void check(const char*s,int ok){put(s);put(ok?":P ":":F ");if(!ok)good=0;}
static double recurrence(double a,double b,double c,int n)
{ while(n--)a=a*b+c;return a; }
static double two(double x,int n)
{ double y=1;while(n--){x=x*0.5+1;y=y+x;}return x+y; }
static double quotient(double x,int n)
{ while(n--)x=(x-1)/2;return x; }
static double chained(double x)
{ double a=2,b=3;a=b=(a+b)*x;return a+b; }
static double assignment_value(double x)
{ double a;return (a=x*x)+x; }
static double snapshot(double x)
{ double a,b;a=x*x;b=a;a=a+x;return a+b; }
static double escaped(double x)
{ double a=x;double *p=&a;a=a*2+1;*p+=1;return a; }
static double volatile_local(double x,int n)
{ volatile double a=x;while(n--)a=a*2+1;return a; }
static double volatile_cast(double x)
{ double a=x;*(volatile double*)&a=a*2+1;return a; }
static double packed_local(double x)
{ struct packed p;p.c=7;p.d=x;p.d=p.d*2+1;return p.d; }
static double sum3(double a,double b,double c){return a+b+c;}
static double pending(double x)
{ return sum3(x*2,({ double y=x;y=y*y;y; }),x+1); }
static double gotoloop(double x,int n)
{ double s=0;again:if(!n--)return s;s=s+x;goto again; }
static double for_step(double x,int n)
{ int i;for(i=0;i<n;x=x*2)i++;return x; }
static double branch(double a,int t)
{ if(t)a=a*2+1;else a=a*3-1;return a; }
static double minus_zero(double x)
{ double a=x;a=a*2;return a; }
static double mixed(double x)
{ double a=x;float b=1;a=a*2+b;b=(float)a;a=a+b;return a; }
static double wide(double x)
{ volatile char pad[17000];double a=x;int i;pad[16999]=3;
  for(i=0;i<4;i++)a=a*2+pad[16999];return a; }
void test_main(void)
{
    bits in,out;
    check("recurrence",recurrence(1,1,2,30)==61);
    check("two-locals",two(2,80)==163);
    check("quotient",quotient(33,5)==0.0625);
    check("chained",chained(2)==20);
    check("assignment-value",assignment_value(3)==12);
    check("snapshot",snapshot(3)==21);
    check("escaped",escaped(3)==8);
    check("volatile",volatile_local(1,3)==15);
    check("volatile-cast",volatile_cast(3)==7);
    check("packed",packed_local(3)==7);
    check("pending",pending(3)==19);
    check("goto",gotoloop(2,23)==46);
    check("for-step",for_step(1,5)==32);
    check("branch",branch(3,0)==8 && branch(3,1)==7);
    in.w[0]=0;in.w[1]=0x80000000u;out.d=minus_zero(in.d);
    check("minus-zero",out.w[0]==0 && out.w[1]==0x80000000u);
    check("mixed",mixed(3)==14);
    check("wide",wide(1)==61);
    put(good?"\nDOUBLE-STORE PASS\n":"\nDOUBLE-STORE FAIL\n");
}
