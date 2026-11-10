/* A long long or double member of a packed struct passed to a call - written
 * out, or the soft-float helper behind `p->d * 2` on a T425/T414 - is read
 * from its bytes: ldnl at an odd address reads the containing words.
 * Expected: Sll-ptr:P ll-ptr2:P ll-local:P ll-global:P ll-index:P ll-two:P ll-helper:P d-ptr:P d-local:P d-global:P d-arith:P d-compare:P d-convert:P d-update:P d-square:P d-dot:P f-ptr:P f-arith:P f-compare:P mixed:P neighbours:P \nPACKED-WIDE-ARG PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)
typedef unsigned long long u64;
struct __attribute__((packed)) pl { char c;u64 v;char e; };
struct __attribute__((packed)) pd { char c;double d;char e; };
struct __attribute__((packed)) pf { char c;float f;char e; };
struct __attribute__((packed)) p2 { char c;short s;u64 v;double d;char e; };
static int good=1;
static struct pl gl;
static struct pd gd;
static void put(const char*s){while(*s)UART=(unsigned char)*s++;}
static void check(const char*s,int ok){put(s);put(ok?":P ":":F ");if(!ok)good=0;}
static u64 id(u64 v){return v;}
static u64 two(u64 a,int k,u64 b){return a-b+k;}
static double did(double d){return d;}
static double dsum(double a,int k,double b){return a+b+k;}
static u64 ll_ptr(struct pl *p){return id(p->v);}
static u64 ll_ptr2(struct pl **pp){return id((*pp)->v);}
static u64 ll_index(struct pl *p,int i){return id(p[i].v);}
static u64 ll_two(struct pl *p,struct pl *q){return two(p->v,3,q->v);}
static u64 ll_helper(struct pl *p,int n){return p->v/n+(p->v>>n)+p->v*n;}
static double d_ptr(struct pd *p){return did(p->d);}
static double d_arith(struct pd *p){return p->d*2+p->d/4-p->c;}
static int d_compare(struct pd *p,struct pd *q)
{ return (p->d<q->d)+2*(p->d==q->d)+4*(q->d>=p->d); }
static int d_convert(struct pd *p){return (int)p->d+(int)(float)p->d;}
static double d_update(struct pd *p,double x)
{ p->c=7;p->d=x;p->d=p->d*2+1;p->d+=p->c;return p->d+p->c; }
static double d_square(struct pd *p){return p->d*p->d;}
static double d_dot(struct pd *p,struct pd *q){return p->d*q->d+q->d*p->d-p->d;}
static float fid(float f){return f;}
static float f_ptr(struct pf *p){return fid(p->f);}
static float f_arith(struct pf *p,struct pf *q){return p->f*q->f+p->f*p->f-q->f/2;}
static int f_compare(struct pf *p,struct pf *q)
{ return (p->f<q->f)+2*(p->f==q->f)+4*(q->f>=p->f); }
static double mixed(struct p2 *p){return dsum(p->d,p->s,(double)(int)id(p->v));}
void test_main(void)
{
    struct pl l[2],*lp=l;
    struct pd d,e;
    struct p2 m;
    struct pf f,g;
    u64 big=0x0123456789abcdefULL;

    l[0].c=0x11;l[0].v=big;l[0].e=0x22;
    l[1].c=0x33;l[1].v=~big;l[1].e=0x44;
    check("ll-ptr",ll_ptr(l)==big);
    check("ll-ptr2",ll_ptr2(&lp)==big);
    check("ll-local",id(l[1].v)==~big);
    gl.c=1;gl.v=big+1;gl.e=2;
    check("ll-global",id(gl.v)==big+1);
    check("ll-index",ll_index(l,1)==~big);
    check("ll-two",ll_two(l,l+1)==big-~big+3);
    check("ll-helper",ll_helper(l,3)==big/3+(big>>3)+big*3);
    d.c=5;d.d=6.5;d.e=9;e.c=1;e.d=8.25;e.e=2;
    check("d-ptr",d_ptr(&d)==6.5);
    check("d-local",did(e.d)==8.25);
    gd.c=3;gd.d=-0.75;gd.e=4;
    check("d-global",did(gd.d)==-0.75 && gd.d*4==-3);
    check("d-arith",d_arith(&d)==13+1.625-5);
    check("d-compare",d_compare(&d,&e)==5 && d_compare(&d,&d)==6);
    check("d-convert",d_convert(&e)==16);
    check("d-update",d_update(&d,3)==21 && d.d==14);
    e.d=8.25;d.d=1.5;
    check("d-square",d_square(&d)==2.25);
    check("d-dot",d_dot(&d,&e)==2*1.5*8.25-1.5);
    f.c=1;f.f=1.5f;f.e=2;g.c=3;g.f=4;g.e=4;
    check("f-ptr",f_ptr(&f)==1.5f);
    check("f-arith",f_arith(&f,&g)==6+2.25f-2);
    check("f-compare",f_compare(&f,&g)==5 && f_compare(&g,&g)==6);
    m.c=1;m.s=-4;m.v=10;m.d=2.5;m.e=6;
    check("mixed",mixed(&m)==8.5);
    check("neighbours",l[0].c==0x11 && l[0].e==0x22 && l[1].c==0x33 &&
          l[1].e==0x44 && d.c==7 && d.e==9 && e.c==1 && e.e==2 && gl.c==1 &&
          gl.e==2 && f.c==1 && f.e==2 && g.c==3 && g.e==4 && gd.c==3 &&
          gd.e==4 && m.c==1 && m.s==-4 && m.e==6);
    put(good?"\nPACKED-WIDE-ARG PASS\n":"\nPACKED-WIDE-ARG FAIL\n");
}
