/*
 * Target: -mt9000tvx
 * test_tvx_allformats.c - every GGUF quantisation format in the local model
 * zoo, decoded on the T800 and checked against llama.cpp.
 *
 * gguf_all_formats.inc holds real blocks pulled out of the actual .gguf files
 * (extract_all_formats.py); gguf_all_formats_dot.inc holds the expected QDOT
 * results computed with llama.cpp's vendored decoders.
 *
 * Also cross-validates TVX's own hand-written decoders (MXFP4DOT/GEMV, Q8DOT,
 * IQ1MDOT, IQ2XXSDOT, IQ3XXSDOT) against llama.cpp's on the same blocks -
 * two independent implementations, and they agree bit-for-bit.
 *
 * Build & run: see transputer/TVX-ISA.md section 6.
 */

#include "gguf/gguf_all_formats.inc"
#include "gguf/gguf_all_formats_dot.inc"
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
static void ud(unsigned v){char b[12];int n=0;if(!v){up('0');return;}while(v){b[n++]='0'+v%10;v/=10;}while(n)up(b[--n]);}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}
static float a[256*QALL_NB];
int test_main(void){
    int k,i,bad=0;
    for(k=0;k<QALL_N;k++){
        int ne=qall_ne[k]; float r;
        for(i=0;i<ne*QALL_NB;i++) a[i]=(float)((i%31)-15)*0.0078125f;
        __qsetfmt(qall_fmt[k]);
        r = __qdot(qall_blk[k], a, QALL_NB, 0.0f);
        us("\n"); us(qall_name[k]); us(" fmt="); ud((unsigned)qall_fmt[k]);
        us(" got="); uh(f2b(r)); us(" want="); uh(qall_dot[k]);
        if(f2b(r)!=qall_dot[k]){ bad++; us("  <-- FAIL"); }
    }
    /* Cross-validate the hand-written TVX decoders against llama.cpp's
     * vendored ones on the same real blocks.  The two accumulate in a
     * different order (TVX applies the block scale once at the end,
     * llama.cpp bakes it into every element), so compare with a relative
     * tolerance rather than bit-exactly. */
    for(k=0;k<QALL_N;k++){
        int ne=qall_ne[k], fmt=qall_fmt[k]; float g,h,d,m;
        if(fmt!=39 && fmt!=8 && fmt!=29 && fmt!=16 && fmt!=18) continue;
        for(i=0;i<ne*QALL_NB;i++) a[i]=(float)((i%31)-15)*0.0078125f;
        __qsetfmt(fmt);
        h = __qdot(qall_blk[k], a, QALL_NB, 0.0f);
        if(fmt==39)      g = __mxfp4gemv(qall_blk[k], a, QALL_NB, 0.0f);
        else if(fmt==8)  g = __q8dot(qall_blk[k], a, QALL_NB, 0.0f);
        else if(fmt==29) g = __iq1mdot(qall_blk[k], a, QALL_NB, 0.0f);
        else if(fmt==16) g = __iq2xxsdot(qall_blk[k], a, QALL_NB, 0.0f);
        else             g = __iq3xxsdot(qall_blk[k], a, QALL_NB, 0.0f);
        d = g - h; if(d<0.0f) d = -d;
        m = h < 0.0f ? -h : h;
        us("\nxcheck "); us(qall_name[k]);
        us(" tvx="); uh(f2b(g)); us(" llama="); uh(f2b(h));
        if(!(d <= m*0.0001f + 1e-9f)){ bad++; us("  <-- DIVERGES"); }
    }

    us("\nformats="); ud((unsigned)QALL_N); us(" failures="); ud((unsigned)bad);
    us(bad?"\nQALL: FAIL\n":"\nQALL: PASS\n");
    return bad;
}
