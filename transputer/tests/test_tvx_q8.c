/*
 * Target: -mt9000tvx
 * test_tvx_q8.c - Q8_0 and binary16 against REAL DeepSeek attention weights.
 *
 * deepseek_q8_attn.inc holds 128 Q8_0 blocks (one K=4096 row) taken verbatim
 * from blk.0.attn_kv.weight of the Q4_K_XL model, with expected values from
 * q8_ref_llamacpp.c (llama.cpp dequantize_row_q8_0 semantics).
 * Q8_0 is used by BOTH DeepSeek variants for attention and shared experts.
 */

#include "gguf/deepseek_q8_attn.inc"
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}
static float b2f(unsigned u){union{float f;unsigned u;}v;v.u=u;return v.f;}
#define K (Q8_NB*32)
static float a[K];
static unsigned short abf[K];
int test_main(void){
    int b,j,i,bad=0; unsigned h=2166136261u; float acc,d,r2;
    /* decode every element with F16TOR32 + int8, hash it */
    for(b=0;b<Q8_NB;b++){
        unsigned hd = (unsigned)q8blk[b*34] | ((unsigned)q8blk[b*34+1]<<8);
        d = b2f(__f16tof32(hd));
        for(j=0;j<32;j++){
            int q = (int)(signed char)q8blk[b*34+2+j];
            unsigned u = f2b((float)q * d); int k;
            for(k=0;k<4;k++){ h^=(u>>(8*k))&0xFF; h*=16777619u; }
        }
    }
    us("q8 decode hash="); uh(h); us(" want="); uh(Q8_HASH);
    if(h!=Q8_HASH) bad=1;
    for(i=0;i<K;i++) a[i]=(float)((i%23)-11)*0.03125f;
    acc = __q8dot(q8blk, a, Q8_NB, 0.0f);
    us("\nq8dot="); uh(f2b(acc)); us(" want="); uh(Q8_DOT);
    if(f2b(acc)!=Q8_DOT) bad=1;
    /* bf16-activation variant must equal the f32 one fed identical values */
    for(i=0;i<K;i++){ abf[i]=(unsigned short)__f32tobf16(f2b(a[i])); a[i]=b2f(__bf16tof32(abf[i])); }
    acc = __q8dot(q8blk, a, Q8_NB, 0.0f);
    r2  = __q8dotbf(q8blk, abf, Q8_NB, 0.0f);
    us("\nq8dotbf="); uh(f2b(r2)); us(" vs f32="); uh(f2b(acc));
    if(f2b(r2)!=f2b(acc)) bad=1;
    us(bad?"\nQ8: FAIL\n":"\nQ8: PASS\n");
    return bad;
}
