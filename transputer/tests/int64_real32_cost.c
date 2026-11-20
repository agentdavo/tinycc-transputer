typedef unsigned int u32;
struct words { u32 lo, hi; };
extern void tcwg_down(const void *, void *);
extern void tcwg_current(const void *, void *);
extern unsigned transputer_ldtimer(void);
volatile struct words in = {0,0x10000010};
volatile u32 result, sink;
static void putc_(int c) { *(volatile unsigned *)0x10000000u = (unsigned char)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex(u32 n) { int i; for(i=28;i>=0;i-=4) putc_("0123456789abcdef"[(n>>i)&15]); }
void test_main(void) {
 u32 i,a,b,oldbits,newbits;
 a=transputer_ldtimer(); for(i=0;i<1000;i++){tcwg_current((const void*)&in,(void*)&result);sink=result;} b=transputer_ldtimer()-a;oldbits=result;
 puts_("CURRENT ");hex(b);putc_(' ');hex(oldbits);putc_('\n');
 a=transputer_ldtimer(); for(i=0;i<1000;i++){tcwg_down((const void*)&in,(void*)&result);sink=result;} b=transputer_ldtimer()-a;newbits=result;
 puts_("DIRECTED ");hex(b);putc_(' ');hex(newbits);putc_('\n');
}
