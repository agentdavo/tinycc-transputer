/*
 * Target: -mt9000tvx
 * test_tvx_simd.c - on-target check for the TVX sub-word SIMD instructions
 * (8 MXFP4 lanes / 2 bfloat16 lanes per 32-bit word) and for MXFP4GEMV.
 *
 * Each wide instruction is checked lane by lane against the already-verified
 * scalar instruction, and MXFP4GEMV is checked against the equivalent chain
 * of MXFP4DOTs (they are defined to be bit-identical).
 *
 * Build & run: see transputer/TVX-ISA.md section 6.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
static void ud(unsigned v){char b[12];int n=0;if(!v){up('0');return;}while(v){b[n++]='0'+v%10;v/=10;}while(n)up(b[--n]);}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}
static int bad=0, ck=0;
static void eq(const char*w,unsigned g,unsigned e){ck++;if(g!=e){bad++;us("\nFAIL ");us(w);us(" got=");uh(g);us(" want=");uh(e);}}
static float src[8];
static float dst[8];
static unsigned char blocks[4*17];
static float acts[4*32];
int test_main(void){
    int i,b; unsigned w, w2; float a,c,r1,r2;
    w = 0x76543210u;
    __mxfp4unpk8(w, dst);
    for(i=0;i<8;i++) eq("unpk8", f2b(dst[i]), __mxfp4tof32((w>>(4*i))&0xF));
    w = 0xFEDCBA98u;
    __mxfp4unpk8(w, dst);
    for(i=0;i<8;i++) eq("unpk8b", f2b(dst[i]), __mxfp4tof32((w>>(4*i))&0xF));
    for(i=0;i<8;i++) src[i] = (float)(i-4) * 0.75f;
    w = __f32tomxfp4x8(src);
    for(i=0;i<8;i++) eq("q8", (w>>(4*i))&0xF, __f32tomxfp4(f2b(src[i])));
    a = 1.5f; c = -0.25f;
    w  = __bf16pack(f2b(a), f2b(c));
    w2 = __bf16pack(f2b(2.0f), f2b(3.0f));
    {
      unsigned m = __bf16mul2(w, w2);
      unsigned s = __bf16add2(w, w2);
      eq("mul2.lo", m & 0xFFFF, __f32tobf16(f2b(-0.25f*3.0f)));
      eq("mul2.hi", m >> 16,    __f32tobf16(f2b(1.5f*2.0f)));
      eq("add2.lo", s & 0xFFFF, __f32tobf16(f2b(-0.25f+3.0f)));
      eq("add2.hi", s >> 16,    __f32tobf16(f2b(1.5f+2.0f)));
    }
    for(i=0;i<4*17;i++) blocks[i]=(unsigned char)(0x31+i*7);
    for(b=0;b<4;b++) blocks[b*17]=(unsigned char)(125+b);
    for(i=0;i<4*32;i++) acts[i]=(float)((i%13)-6)*0.375f;
    r1 = 0.5f;
    for(b=0;b<4;b++) r1 = __mxfp4dot(&blocks[b*17], &acts[b*32], r1);
    r2 = __mxfp4gemv(blocks, acts, 4, 0.5f);
    eq("gemv==4xdot", f2b(r2), f2b(r1));
    us("\nchecks="); ud((unsigned)ck); us(" failures="); ud((unsigned)bad);
    us(bad?"\nSIMD: FAIL\n":"\nSIMD: PASS\n");
    return bad;
}
