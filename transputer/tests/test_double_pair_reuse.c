/* Reuse only a consumed contiguous double pair.  Other pending values,
 * including a value's high word, must survive nested arithmetic and calls.
 * Expected: Schain:P pair-pair:P subtract:P divide:P three-products:P deep:P call-args:P indirect:P assignment:P branch:P call-results:P float-mixed:P long-double:P loop:P minus-zero:P nan:P wide:P \nDOUBLE-PAIR PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)
typedef union { double d;unsigned w[2]; } bits;
static int good=1;
static void put(const char*s){while(*s)UART=(unsigned char)*s++;}
static void check(const char*s,int ok){put(s);put(ok?":P ":":F ");if(!ok)good=0;}
static double id(double x){return x;}
static double chain(double a,double b,double c,double d,double e)
{ return (a*b+c)*d+e; }
static double pair_pair(double a,double b,double c,double d)
{ return (a+b)*(c+d); }
static double subtract(double a,double b,double c,double d)
{ return a*b-c*d; }
static double divide(double a,double b,double c,double d)
{ return (a+b)/(c-d); }
static double three(double a,double b,double c,double d,double e,double f)
{ return a*b+c*d+e*f; }
static double deep(double a,double b,double c,double d,double e,double f)
{ return (a*b+c*d)-(e*f+(a+c)*(b+d)); }
static double hold(double a,double b,double c,double d)
{ return a+2*b+4*c+8*d; }
static double args(double a,double b,double c)
{ return hold(a*b+c,a*b-c,(a+b)*(a-b),(a+b)/(a-b)); }
static double (*volatile indirect)(double,double,double,double)=hold;
static double indirect_args(double a,double b,double c)
{ return indirect(a*b+c,a*b-c,(a+b)*(a-b),(a+b)/(a-b)); }
static double assignment(double a,double b)
{ double x;double y=(x=a+b)*(a-b);return x+y; }
static double branch(int flag,double a,double b,double c,double d)
{ return (flag?(a+b)*(c+d):(a-b)*(c-d))+(a*c+b*d); }
static double call_results(double a,double b)
{ return id(a+b)*id(a-b)+id(a*b); }
static double float_mixed(double a,double b,double c,double d)
{ return (double)(float)(a*b+c*d)+(a+b)*(c+d); }
static long double extended(long double a,long double b,long double c)
{ return (a+b)*(a-b)+c; }
static double product_loop(double a,double b,double c,double d,double e,double f,int n)
{ while(n--)a=(a*b+c*d)*(e+f);return a; }
static double zero_chain(double x){return (x*2)*3;}
static double wide(double a,double b)
{ volatile char pad[17000];pad[16999]=3;return (a+b)*(a-b)+pad[16999]; }
void test_main(void)
{
    bits in,out;
    check("chain",chain(2,3,4,5,6)==56);
    check("pair-pair",pair_pair(2,3,4,5)==45);
    check("subtract",subtract(2,3,4,5)==-14);
    check("divide",divide(2,3,9,4)==1);
    check("three-products",three(2,3,4,5,1,2)==28);
    check("deep",deep(2,3,4,5,1,2)==-24);
    check("call-args",args(3,1,2)==55);
    check("indirect",indirect_args(3,1,2)==55);
    check("assignment",assignment(3,1)==12);
    check("branch",branch(1,2,3,4,5)==68 && branch(0,2,3,4,5)==24);
    check("call-results",call_results(3,1)==11);
    check("float-mixed",float_mixed(2,3,4,5)==71);
    check("long-double",extended(3,1,2)==10);
    check("loop",product_loop(1,1,1,1,0,1,20)==21);
    in.w[0]=0;in.w[1]=0x80000000u;out.d=zero_chain(in.d);
    check("minus-zero",out.w[0]==0 && out.w[1]==0x80000000u);
    in.w[0]=0x12345678u;in.w[1]=0x7ff80000u;out.d=zero_chain(in.d);
    check("nan",(out.w[1]&0x7ff00000u)==0x7ff00000u && ((out.w[1]&0xfffffu)||out.w[0]));
    check("wide",wide(3,1)==11);
    put(good?"\nDOUBLE-PAIR PASS\n":"\nDOUBLE-PAIR FAIL\n");
}
