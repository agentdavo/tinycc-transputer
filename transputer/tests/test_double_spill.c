/*
 * test_double_spill.c - regression test for the two-word (64-bit) spill bug.
 *
 * store() stores ONE register.  For a two-word value the generic code calls
 * it twice - store(r) then store(r2) with the address advanced by PTR_SIZE
 * (save_reg_upstack in tccgen.c, and vstore() which narrows the type first).
 *
 * The transputer backend's float-register path used to write BOTH words, so
 * the caller's second store() emitted another pair one word further on,
 * running past the end of the spill temporary and overwriting the next local.
 * The visible symptom was that the *second* of two int->float conversions
 * passed as call arguments silently read back as 0.0, because spilling the
 * first argument trampled the source variable of the second.
 *
 * Only reproduces when the sources are *locals* (globals are addressed
 * differently and do not sit next to the spill temporaries), so keep them
 * local here.
 *
 * Build & run:
 *   ./transputer-tcc -nostdlib -nostdinc -I include -c -o /tmp/ds.o \
 *       transputer/tests/test_double_spill.c
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/boot.o transputer/tests/test_boot.S
 *   ./transputer-tcc -nostdlib -nostdinc -o /tmp/ds.elf /tmp/boot.o /tmp/ds.o
 *   ./transputer-netload /tmp/ds.elf /tmp/ds.btl
 *   DISPLAY= timeout 60 transputer/t4/t4 -s8 -si -se -sb /tmp/ds.btl
 */
#include <stdarg.h>

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
static void ud(unsigned v){char b[12];int n=0;if(!v){up('0');return;}while(v){b[n++]='0'+v%10;v/=10;}while(n)up(b[--n]);}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}
static int bad=0, ck=0;
static void eq(const char*w,unsigned g,unsigned e)
{ ck++; if(g!=e){bad++;us("\nFAIL ");us(w);us(" got=");uh(g);us(" want=");uh(e);} }

static void two_f(float x, float y)
{ eq("proto f,f 1st", f2b(x), f2b(7.0f)); eq("proto f,f 2nd", f2b(y), f2b(7.0f)); }

static void two_d(double x, double y)
{ eq("proto d,d 1st", f2b((float)x), f2b(7.0f)); eq("proto d,d 2nd", f2b((float)y), f2b(7.0f)); }

static void three_d(double x, double y, double z)
{ eq("proto 3d 1st", f2b((float)x), f2b(7.0f));
  eq("proto 3d 2nd", f2b((float)y), f2b(7.0f));
  eq("proto 3d 3rd", f2b((float)z), f2b(7.0f)); }

static void varg2(const char *t, ...)
{ va_list ap; double x,y; va_start(ap,t); x=va_arg(ap,double); y=va_arg(ap,double); va_end(ap);
  eq("varargs 1st", f2b((float)x), f2b(7.0f)); eq("varargs 2nd", f2b((float)y), f2b(7.0f)); }

/* Each case gets its own function with a MINIMAL frame.  The corruption
 * lands on whatever local follows the spill temporary, so extra locals in
 * scope change the layout and can hide it - keep these frames bare. */
static void case_proto_f(void){ int a=7,b=7; two_f((float)a,(float)b); }
static void case_proto_d(void){ int a=7,b=7; two_d((float)a,(float)b); }
static void case_proto_dd(void){ int a=7,b=7; two_d((double)a,(double)b); }
static void case_three(void){ int a=7,b=7,c=7; three_d((double)a,(double)b,(double)c); }
static void case_varargs(void){ int a=7,b=7; varg2("t",(float)a,(float)b); }
static void case_float_src(void){ float a=7.0f,b=7.0f; two_f(a,b); two_d(a,b); }
static void case_double_src(void){ double a=7.0,b=7.0; two_d(a,b); varg2("t",a,b); }
static void case_survives(void){ int a=7,b=7; double keep=(double)a;
    two_d((double)b,(double)b); eq("survives call", f2b((float)keep), f2b(7.0f)); }

int test_main(void)
{
    case_proto_f();
    case_proto_d();
    case_proto_dd();
    case_three();
    case_varargs();
    case_float_src();
    case_double_src();
    case_survives();

    us("\nchecks="); ud((unsigned)ck); us(" failures="); ud((unsigned)bad);
    us(bad ? "\nDBLSPILL: FAIL\n" : "\nDBLSPILL: PASS\n");
    return bad;
}
