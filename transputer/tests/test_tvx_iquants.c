/*
 * Target: -mt9000tvx
 * test_tvx_iquants.c - IQ1_M / IQ2_XXS / IQ3_XXS against REAL weights from
 * DeepSeek-V4-Flash-0731-UD-IQ1_M, which is 97% these three formats.
 *
 * Expected values come from iq_ref_llamacpp.c, which compiles llama.cpp's
 * *actual* dequantize_row_iq{1_m,2_xxs,3_xxs} functions (extracted verbatim
 * from ggml-quants.c) against its real codebooks - so this is a check against
 * upstream code, not against a re-transcription of it.
 */

#include "gguf/deepseek_iquants.inc"
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}
#define K (IQ_NB*256)
static float a[K];
int test_main(void){
    int i,bad=0; float r;
    for(i=0;i<K;i++) a[i]=(float)((i%29)-14)*0.015625f;
    r = __iq1mdot(IQ1M_blk, a, IQ_NB, 0.0f);
    us("iq1m  ="); uh(f2b(r)); us(" want="); uh(IQ1M_DOT); if(f2b(r)!=IQ1M_DOT) bad=1;
    r = __iq2xxsdot(IQ2XXS_blk, a, IQ_NB, 0.0f);
    us("\niq2xxs="); uh(f2b(r)); us(" want="); uh(IQ2XXS_DOT); if(f2b(r)!=IQ2XXS_DOT) bad=1;
    r = __iq3xxsdot(IQ3XXS_blk, a, IQ_NB, 0.0f);
    us("\niq3xxs="); uh(f2b(r)); us(" want="); uh(IQ3XXS_DOT); if(f2b(r)!=IQ3XXS_DOT) bad=1;
    us(bad?"\nIQ: FAIL\n":"\nIQ: PASS\n");
    return bad;
}
