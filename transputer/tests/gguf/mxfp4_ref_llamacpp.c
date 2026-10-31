/* Host reference using llama.cpp's EXACT MXFP4 semantics, to validate that
 * the transputer's MXFP4 instructions consume GGUF bytes identically. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
static const int8_t kvalues_mxfp4[16] = {0,1,2,3,4,6,8,12,0,-1,-2,-3,-4,-6,-8,-12};
static float e8m0_half(uint8_t x){ return ldexpf(1.0f,(int)x-127) * 0.5f; }
static uint32_t f2b(float f){union{float f;uint32_t u;}v;v.f=f;return v.u;}
#define NB 128
#define K  (NB*32)
int main(void){
    static uint8_t blk[NB*17];
    static float w[K], a[K];
    FILE *fp=fopen("/tmp/expert.bin","rb");
    uint32_t h=2166136261u; int i,b,j; double acc_ref; float acc;
    if(fread(blk,1,sizeof blk,fp)!=sizeof blk){printf("read fail\n");return 1;}
    fclose(fp);
    /* llama.cpp dequantize_row_mxfp4 */
    for(b=0;b<NB;b++){
        float d=e8m0_half(blk[b*17]);
        const uint8_t *qs=&blk[b*17+1];
        for(j=0;j<16;j++){
            w[b*32+j]      = kvalues_mxfp4[qs[j]&0x0F]*d;
            w[b*32+j+16]   = kvalues_mxfp4[qs[j]>>4]  *d;
        }
    }
    /* FNV-1a over the decoded weight bit patterns */
    for(i=0;i<K;i++){ uint32_t u=f2b(w[i]); int k;
        if(u==0x80000000u) u=0u;          /* normalise the sign of zero */
        for(k=0;k<4;k++){ h^=(u>>(8*k))&0xFF; h*=16777619u; } }
    /* deterministic activations, same formula the target uses */
    for(i=0;i<K;i++) a[i]=(float)((i%17)-8)*0.0625f;
    /* GEMV in the order MXFP4GEMV defines: per block, sequential f32,
       scale applied once after the inner accumulation */
    acc=0.0f;
    for(b=0;b<NB;b++){
        float t=0.0f, s=ldexpf(1.0f,(int)blk[b*17]-127);
        for(j=0;j<32;j++){
            /* E2M1 value = kvalues/2 */
            float e=(float)kvalues_mxfp4[ (j<16) ? (blk[b*17+1+j]&0x0F) : (blk[b*17+1+j-16]>>4) ] * 0.5f;
            t = t + e*a[b*32+j];
        }
        acc = acc + s*t;
    }
    acc_ref=acc;
    printf("#define REF_NB %d\n", NB);
    printf("#define REF_HASH 0x%08Xu\n", h);
    printf("#define REF_GEMV 0x%08Xu\n", f2b(acc));
    printf("/* scale bytes of first 4 blocks: %u %u %u %u */\n",
           blk[0],blk[17],blk[34],blk[51]);
    printf("/* w[0]=%g w[1]=%g w[16]=%g w[4095]=%g gemv=%.9g */\n",
           w[0],w[1],w[16],w[K-1],acc_ref);
    /* emit the raw block bytes as a C array */
    printf("static const unsigned char refblk[%d] = {\n", NB*17);
    for(i=0;i<NB*17;i++){ printf("%u%s", blk[i], i+1<NB*17?",":""); if(i%20==19) printf("\n"); }
    printf("};\n");
    return 0;
}
