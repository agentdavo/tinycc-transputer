/*
 * Target: -mt9000tvx
 * test_tvx_deepseek.c - run REAL DeepSeek-V4-Flash-0731 MXFP4 weights on the
 * T800 and check the result against llama.cpp's own dequantisation.
 *
 * gguf/deepseek_v4_block0.inc holds 128 MXFP4 blocks (one full K=4096 row) taken
 * verbatim from blk.0.ffn_gate_exps.weight of
 *   DeepSeek-V4-Flash-0731-UD-Q4_K_XL-00002-of-00005.gguf
 * together with expected values computed by mxfp4_ref_llamacpp.c, which uses
 * llama.cpp's exact kvalues_mxfp4 table and E8M0 half-scale convention.
 *
 * Regenerate with (in gguf/, beside the two tools):
 *   python3 gguf_extract.py <shard2.gguf> blk.0.ffn_gate_exps.weight 128 > /tmp/expert.bin
 *   cc -O2 -o /tmp/ref mxfp4_ref_llamacpp.c -lm && /tmp/ref > deepseek_v4_block0.inc
 *
 * Expected: Sdecode hash=5de92837 want=5de92837\ngemv=3f28a000 want=3f28a000\nw0=bc800000\nDEEPSEEK: PASS\nH
 *
 * Checks:
 *   1. every one of the 4096 elements decodes to llama.cpp's value
 *      (sign of zero normalised - see below);
 *   2. MXFP4GEMV over the whole row is bit-identical to the reference.
 *
 * Note on zero: OCP E2M1 defines code 8 as -0, while llama.cpp's kvalues
 * table maps it to +0.  6.3% of the elements in this row are code 8.  The
 * values are numerically identical, which is why check 2 matches exactly.
 */

/* Run real DeepSeek-V4-Flash MXFP4 expert weights on the T800. */
#include "gguf/deepseek_v4_block0.inc"
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}
static float b2f(unsigned u){union{float f;unsigned u;}v;v.u=u;return v.f;}
#define K (REF_NB*32)
static float a[K];
static float w[32];
int test_main(void){
    int b,j,i,bad=0;
    unsigned h=2166136261u;
    float acc, sc;

    /* decode every element with the transputer's own instructions and hash it,
       exactly as the host did with llama.cpp's formula */
    for(b=0;b<REF_NB;b++){
        sc = b2f(__mxe8m0tof32(refblk[b*17]));
        for(j=0;j<32;j++){
            unsigned code = (j<16) ? (refblk[b*17+1+j] & 0x0F)
                                   : (refblk[b*17+1+j-16] >> 4);
            float e = b2f(__mxfp4tof32(code)) * sc;
            unsigned u = f2b(e); int k;
            /* OCP E2M1 defines code 8 as -0; GGUF's table maps it to +0.
               Numerically identical, so normalise before hashing. */
            if(u == 0x80000000u) u = 0u;
            for(k=0;k<4;k++){ h ^= (u>>(8*k))&0xFF; h *= 16777619u; }
        }
    }
    us("decode hash="); uh(h); us(" want="); uh(REF_HASH);
    if(h != REF_HASH) bad=1;

    /* same activations as the host reference */
    for(i=0;i<K;i++) a[i]=(float)((i%17)-8)*0.0625f;

    /* one instruction per 32 weights, 128 blocks = the whole K=4096 row */
    acc = __mxfp4gemv(refblk, a, REF_NB, 0.0f);
    us("\ngemv="); uh(f2b(acc)); us(" want="); uh(REF_GEMV);
    if(f2b(acc) != REF_GEMV) bad=1;

    /* dequantise one block through the 8-lane path and spot-check */
    {
        unsigned lo,hi; float s0;
        s0 = b2f(__mxe8m0tof32(refblk[0]));
        lo=0; hi=0;
        for(i=0;i<8;i++){ lo |= (unsigned)(refblk[1+i]&0x0F)<<(4*i);
                          hi |= (unsigned)(refblk[1+i]>>4)<<(4*i); }
        __mxfp4unpk8(lo, w);
        __mxfp4unpk8(hi, &w[16]);
        us("\nw0="); uh(f2b(w[0]*s0));
    }
    us(bad?"\nDEEPSEEK: FAIL\n":"\nDEEPSEEK: PASS\n");
    return bad;
}
