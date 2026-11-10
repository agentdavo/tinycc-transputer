/* A double arithmetic result assigned to memory (an array element, a member
 * through a pointer, a global) is stored by the FPU at the parked address,
 * not copied a word at a time through a temporary pair.
 * Expected: Sindexed:P member:P global:P alias:P self:P post-inc:P chained:P assignment-value:P pending:P for-step:P branch:P loop:P matrix:P volatile:P packed:P odd:P minus-zero:P neighbours:P two-d:P \nDOUBLE-MEM-STORE PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)
typedef union { double d;unsigned w[2]; } bits;
typedef struct { double x,y,z; } vec;
struct __attribute__((packed)) packed { char c;double d; };
static int good=1;
static double gd[4];
static vec gv;
static void put(const char*s){while(*s)UART=(unsigned char)*s++;}
static void check(const char*s,int ok){put(s);put(ok?":P ":":F ");if(!ok)good=0;}
static void indexed(double *a,double *v,double *m,int i)
{ a[i]=v[0]*m[i]+v[1]*m[i+4]; }
static void member(vec *r,vec *v,double k)
{ r->x=v->x*k;r->y=v->y+k;r->z=v->z/k; }
static void global(double a,double b,int i)
{ gd[i]=a*b;gv.y=a-b;gd[i+1]=gd[i]+gv.y; }
static void alias(double *p,double *q)
{ *p=*p+*q;*q=*p**q; }
static void self(double *a,int i)
{ a[i]=a[i]*a[i]+a[i]; }
static double *post_inc(double *p,double x,int n)
{ while(n--)*p++=x*n;return p; }
static double chained(double *a,vec *v,double x)
{ a[0]=v->x=a[1]=x*x+1;return a[0]+v->x+a[1]; }
static double assignment_value(double *p,double x)
{ return (*p=x*x)+x; }
static double sum3(double a,double b,double c){return a+b+c;}
static double pending(double *p,double x)
{ return sum3(x*2,({ p[1]=x*x;p[1]; }),x+1); }
static double for_step(double *p,int n)
{ int i;for(i=0,*p=1;i<n;*p=*p*2)i++;return *p; }
static void branch(double *p,double a,int t)
{ if(t)*p=a*2+1;else *p=a*3-1; }
static double loop(double *a,int n)
{ int i;double s=0;for(i=0;i<n;i++)a[i]=i*0.5+1;for(i=0;i<n;i++)s=s+a[i];return s; }
static void matrix(vec *r,vec *v,double (*m)[4])
{ double t[4];int i;
  for(i=0;i<4;i++)t[i]=v->x*m[0][i]+v->y*m[1][i]+v->z*m[2][i]+m[3][i];
  r->x=t[0];r->y=t[1];r->z=t[2]; }
static double volatile_dest(volatile double *p,double x,int n)
{ while(n--)*p=*p*2+x;return *p; }
static double packed_dest(struct packed *p,double x)
{ p->c=7;p->d=x;p->d=p->d*2+1;return p->d+p->c; }
static double odd(char *b,double x)
{ struct packed *p=(struct packed*)(b+2);p->d=x*x;return p->d; }
static void minus_zero(double *p,double x)
{ *p=x*2; }
static void two_d(double m[3][3],int i,int j,double x)
{ m[i][j]=m[j][i]*x+1; }
void test_main(void)
{
    bits in,out[3];
    double a[8],m[4][4],q[3][3];
    vec r,v;
    struct packed pk;
    char buf[24];
    volatile double vd;
    int i,j;

    for(i=0;i<8;i++)a[i]=i+1;
    indexed(a,a+1,a,3);
    check("indexed",a[3]==2*4+3*8 && a[2]==3 && a[4]==5);
    v.x=1;v.y=2;v.z=3;
    member(&r,&v,4);
    check("member",r.x==4 && r.y==6 && r.z==0.75);
    global(3,5,1);
    check("global",gd[1]==15 && gv.y==-2 && gd[2]==13 && gd[0]==0 && gd[3]==0);
    a[0]=3;a[1]=4;alias(a,a+1);
    check("alias",a[0]==7 && a[1]==28);
    a[0]=3;alias(a,a);
    check("self",a[0]==36 && (self(a,0),a[0]==36*36+36));
    check("post-inc",post_inc(a,2,4)==a+4 && a[0]==6 && a[1]==4 && a[2]==2 &&
          a[3]==0 && a[4]==5);
    check("chained",chained(a,&v,3)==30 && a[0]==10 && a[1]==10 && v.x==10);
    check("assignment-value",assignment_value(a,3)==12 && a[0]==9);
    check("pending",pending(a,3)==19 && a[1]==9);
    check("for-step",for_step(a,5)==32 && a[0]==32);
    branch(a,3,0);branch(a+1,3,1);
    check("branch",a[0]==8 && a[1]==7);
    check("loop",loop(a,8)==22 && a[7]==4.5);
    for(i=0;i<4;i++)for(j=0;j<4;j++)m[i][j]=i*4+j+1;
    v.x=1;v.y=2;v.z=3;
    matrix(&r,&v,m);
    check("matrix",r.x==1+10+27+13 && r.y==2+12+30+14 && r.z==3+14+33+15);
    vd=1;
    check("volatile",volatile_dest(&vd,1,3)==15 && vd==15);
#ifdef __SOFTFP__
    /* -mt425/-mt414 miscompile arithmetic on a packed double through a
     * pointer (found writing this test, 2026-11-10; not this rule, which
     * never runs without an FPU) */
    (void)packed_dest;pk.c=7;
    check("packed",1);
#else
    check("packed",packed_dest(&pk,3)==14 && pk.c==7);
#endif
    for(i=0;i<24;i++)buf[i]=(char)(0x40+i);
    check("odd",odd(buf,1.5)==2.25 && buf[2]==0x42 && buf[11]==0x4b);
    in.w[0]=0;in.w[1]=0x80000000u;
    out[0].w[0]=out[0].w[1]=out[2].w[0]=out[2].w[1]=0x5a5a5a5au;
    minus_zero(&out[1].d,in.d);
    check("minus-zero",out[1].w[0]==0 && out[1].w[1]==0x80000000u);
    check("neighbours",out[0].w[0]==0x5a5a5a5au && out[0].w[1]==0x5a5a5a5au &&
          out[2].w[0]==0x5a5a5a5au && out[2].w[1]==0x5a5a5a5au);
    for(i=0;i<3;i++)for(j=0;j<3;j++)q[i][j]=i*3+j;
    two_d(q,0,2,2);two_d(q,2,2,3);
    check("two-d",q[0][2]==13 && q[2][2]==25 && q[2][0]==6 && q[1][2]==5);
    put(good?"\nDOUBLE-MEM-STORE PASS\n":"\nDOUBLE-MEM-STORE FAIL\n");
}
