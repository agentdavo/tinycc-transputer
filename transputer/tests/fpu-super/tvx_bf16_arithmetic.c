/* Original TVX arithmetic tests with independently generated SoftFloat goldens. */
#include "tvx-bf16-vectors.h"
#ifndef TIMING_UNIT
#define TIMING_UNIT "instructions"
#endif
#define DATA (*(volatile unsigned *)0x10000000u)
#define STATUS (*(volatile unsigned *)0x10000004u)
static void out(unsigned c){while(!(STATUS&1)){} DATA=c;}
static void text(const char *p){while(*p)out(*p++);}
static void hex(unsigned x){int i;for(i=28;i>=0;i-=4)out("0123456789abcdef"[(x>>i)&15]);}
unsigned tvx_test_add(unsigned x,unsigned y){return __bf16add2(x,y);}
unsigned tvx_test_mul(unsigned x,unsigned y){return __bf16mul2(x,y);}
unsigned tvx_test_dot(const unsigned char *x,const unsigned char *y,unsigned n,unsigned acc){union {float f;unsigned u;} a,r;a.u=acc;r.f=__bf16dot(x,y,n,a.f);return r.u;}
static unsigned checks,failed;
static void result(unsigned got,unsigned want,unsigned wantflags,unsigned elapsed){
 unsigned gotflags=__ldflags();
 unsigned ok=got==want && gotflags==wantflags;
 text(ok?"TVX-OK ":"TVX-FAIL ");hex(checks);text(" got=");hex(got);text(" want=");hex(want);text(" flags=");hex(gotflags);text(" wantflags=");hex(wantflags);out('\n');
 text("TVX-TIME ");hex(checks);text(" " TIMING_UNIT "=");hex(elapsed);out('\n');checks++;if(!ok)failed++;
}
int tvx_bf16_arithmetic_main(void){
 unsigned i,j,k,start,end,got;
 /* Extra byte permits deliberately unaligned vector inputs. */
 union {unsigned align;unsigned char bytes[64];} x,y;
 text("\nTVX-BF16 BEGIN arithmetic\n");
 for(i=0;i<PAIR_COUNT;i++){
  unsigned a,b,add,mul,af,mf;
  if(i<sizeof(pairs)/sizeof(pairs[0])) {
   a=pairs[i][0];b=pairs[i][1];add=pairs[i][2];mul=pairs[i][3];
   af=(pairs[i][4]&65535)<<2;mf=(pairs[i][4]>>16)<<2;
  } else {
   unsigned n=(i-528)/2,nan=(n<127?0x7f80:0xff80)|(n%127+1);
   unsigned q=nan|64;
   a=((i-528)&1)?nan|(nan<<16):nan|0x3f800000u;
   b=((i-528)&1)?0x7f81ffc2u:0x3f80|(nan<<16);
   add=mul=q|(q<<16);
   af=mf=(((i-528)&1)||!(nan&64))?0x104:4;
  }
  __stflags(0);start=__ldtimer();got=tvx_test_add(a,b);end=__ldtimer();result(got,add,af,end-start);
  __stflags(0);start=__ldtimer();got=tvx_test_mul(a,b);end=__ldtimer();result(got,mul,mf,end-start);
 }
 for(i=0;i<sizeof(dots)/sizeof(dots[0]);i++)for(k=0;k<2;k++){
  for(j=0;j<dots[i].n;j++){
   x.bytes[k+2*j]=dots[i].x[j];x.bytes[k+2*j+1]=dots[i].x[j]>>8;
   y.bytes[k+2*j]=dots[i].y[j];y.bytes[k+2*j+1]=dots[i].y[j]>>8;
  }
  __stflags(0);start=__ldtimer();got=tvx_test_dot(x.bytes+k,y.bytes+k,dots[i].n,dots[i].acc);end=__ldtimer();result(got,dots[i].want,dots[i].flags,end-start);
 }
 text(failed?"TVX-BF16 FAIL checks=":"TVX-BF16 PASS checks=");hex(checks);text(" failed=");hex(failed);out('\n');return failed!=0;
}
#ifdef TVX_ARITH_STANDALONE
int test_main(void){return tvx_bf16_arithmetic_main();}
#endif
