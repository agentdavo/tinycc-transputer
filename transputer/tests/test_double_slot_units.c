/* Known double accesses must move as complete units, including overlapping
 * pairs and the boundary of every outgoing-argument prefix size.
 * Expected: SP1:P P2:P P3:P P4:P P5:P mixed-pairs:P array:P wide:P minus-zero:P volatile:P \nSLOT-DOUBLE PASS\nH
 */
#define UART (*(volatile unsigned*)0x10000000u)
typedef union {double d;unsigned w[2];} bits;
static int good=1;
static void put(const char*s){while(*s)UART=(unsigned char)*s++;}
static void check(const char*s,int ok){put(s);put(ok?":P ":":F ");if(!ok)good=0;}
static double h4(double a,double b){return a+b;}
static double h5(int*p,double a,double b){return a+b+*p;}
static double h6(double a,double b,double c){return a+b+c;}
static double h7(int*p,double a,double b,double c){return a+b+c+*p;}
static double h8(double a,double b,double c,double d){return a+b+c+d;}
static double p1(double x){int i;double y=h4(x,x+1);for(i=0;i<2;i++)y=(y*x+1)/(x+1);return y;}
static double p2(double x){int i,t=1;double y=h5(&t,x,x+1);for(i=0;i<2;i++)y=(y*x+1)/(x+1);return y;}
static double p3(double x){int i;double y=h6(x,x+1,x+2);for(i=0;i<2;i++)y=(y*x+1)/(x+1);return y;}
static double p4(double x){int i,t=1;double y=h7(&t,x,x+1,x+2);for(i=0;i<2;i++)y=(y*x+1)/(x+1);return y;}
static double p5(double x){int i;double y=h8(x,x+1,x+2,x+3);for(i=0;i<2;i++)y=(y*x+1)/(x+1);return y;}
static float fid(float x){return x;}
static double mixed(float f,double a,double b,double c,double d)
{return fid(f)+(a+b)*(c+d);}
static double array(double x)
{double a[3]={x,x+1,x+2};double *end=a+3;a[1]=(a[0]+a[2])*(a[0]-a[2]);return a[1]+(end-a);}
static double wide(double x)
{volatile char pad[17000];double a=x;pad[16999]=3;a=(a+1)*(a-1);return a+pad[16999];}
static double scaled(double x){double a=x;a=a*2;return a;}
static double vol(double x){volatile double y=x;y=(y+1)*(y-1);return y;}
void test_main(void)
{bits in,out;
 check("P1",p1(1)==1.5);check("P2",p2(1)==1.75);check("P3",p3(1)==2.25);
 check("P4",p4(1)==2.5);check("P5",p5(1)==3.25);
 check("mixed-pairs",mixed(0.5f,2,3,4,5)==45.5);check("array",array(2)==-9);
 check("wide",wide(3)==11);in.w[0]=0;in.w[1]=0x80000000u;out.d=scaled(in.d);
 check("minus-zero",out.w[0]==0&&out.w[1]==0x80000000u);check("volatile",vol(3)==8);
 put(good?"\nSLOT-DOUBLE PASS\n":"\nSLOT-DOUBLE FAIL\n");}
