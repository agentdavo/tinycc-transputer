#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#define GGML_RESTRICT restrict
#define GGML_COMMON_DECL_C
#define GGML_COMMON_IMPL_C
#include "ggml/src/ggml-common.h"
typedef uint16_t ggml_half_t;
static float ggml_fp16_to_fp32(ggml_half h){
    uint16_t x; memcpy(&x,&h,2);
    int s=(x>>15)&1,e=(x>>10)&0x1F,m=x&0x3FF; double v;
    if(e==0) v=ldexp((double)m,-24);
    else if(e==31) return s?-INFINITY:INFINITY;
    else v=ldexp(1024.0+(double)m,e-25);
    return (float)(s?-v:v);
}
#undef GGML_FP16_TO_FP32
#define GGML_FP16_TO_FP32(x) ggml_fp16_to_fp32(x)
void dequantize_row_iq1_m(const block_iq1_m * GGML_RESTRICT x, float * GGML_RESTRICT y, int64_t k) {
    assert(k % QK_K == 0);
    const int64_t nb = k / QK_K;

    float delta[4];
    uint16_t idx[4];

    iq1m_scale_t scale;

    for (int i = 0; i < nb; i++) {

        const uint16_t * sc = (const uint16_t *)x[i].scales;
        scale.u16 = (sc[0] >> 12) | ((sc[1] >> 8) & 0x00f0) | ((sc[2] >> 4) & 0x0f00) | (sc[3] & 0xf000);
        const float d = GGML_FP16_TO_FP32(scale.f16);

        const uint8_t * qs = x[i].qs;
        const uint8_t * qh = x[i].qh;

        for (int ib = 0; ib < QK_K/32; ++ib) {
            const float dl1 = d * (2*((sc[ib/2] >> (6*(ib%2)+0)) & 0x7) + 1);
            const float dl2 = d * (2*((sc[ib/2] >> (6*(ib%2)+3)) & 0x7) + 1);

            idx[0] = qs[0] | ((qh[0] << 8) & 0x700);
            idx[1] = qs[1] | ((qh[0] << 4) & 0x700);
            idx[2] = qs[2] | ((qh[1] << 8) & 0x700);
            idx[3] = qs[3] | ((qh[1] << 4) & 0x700);
            delta[0] = qh[0] & 0x08 ? -IQ1S_DELTA : IQ1S_DELTA;
            delta[1] = qh[0] & 0x80 ? -IQ1S_DELTA : IQ1S_DELTA;
            delta[2] = qh[1] & 0x08 ? -IQ1S_DELTA : IQ1S_DELTA;
            delta[3] = qh[1] & 0x80 ? -IQ1S_DELTA : IQ1S_DELTA;
            for (int l = 0; l < 2; ++l) {
                const int8_t * grid = (const int8_t *)(iq1s_grid + idx[l]);
                for (int j = 0; j < 8; ++j) {
                    y[j] = dl1 * (grid[j] + delta[l]);
                }
                y += 8;
            }
            for (int l = 2; l < 4; ++l) {
                const int8_t * grid = (const int8_t *)(iq1s_grid + idx[l]);
                for (int j = 0; j < 8; ++j) {
                    y[j] = dl2 * (grid[j] + delta[l]);
                }
                y += 8;
            }
            qs += 4;
            qh += 2;
        }
    }
}
void dequantize_row_iq2_xxs(const block_iq2_xxs * GGML_RESTRICT x, float * GGML_RESTRICT y, int64_t k) {
    assert(k % QK_K == 0);
    const int64_t nb = k / QK_K;

    uint32_t aux32[2];
    const uint8_t * aux8 = (const uint8_t *)aux32;

    for (int i = 0; i < nb; i++) {

        const float d = GGML_FP16_TO_FP32(x[i].d);

        for (int ib32 = 0; ib32 < QK_K/32; ++ib32) {
            memcpy(aux32, x[i].qs + 4*ib32, 2*sizeof(uint32_t));
            const float db = d * (0.5f + (aux32[1] >> 28)) * 0.25f;
            for (int l = 0; l < 4; ++l) {
                const uint8_t * grid = (const uint8_t *)(iq2xxs_grid + aux8[l]);
                const uint8_t  signs = ksigns_iq2xs[(aux32[1] >> 7*l) & 127];
                for (int j = 0; j < 8; ++j) {
                    y[j] = db * grid[j] * (signs & kmask_iq2xs[j] ? -1.f : 1.f);
                }
                y += 8;
            }
        }
    }
}
void dequantize_row_iq3_xxs(const block_iq3_xxs * GGML_RESTRICT x, float * GGML_RESTRICT y, int64_t k) {
    assert(k % QK_K == 0);
    const int64_t nb = k / QK_K;

    uint32_t aux32;

    for (int i = 0; i < nb; i++) {

        const float d = GGML_FP16_TO_FP32(x[i].d);
        const uint8_t * qs = x[i].qs;
        const uint8_t * scales_and_signs = qs + QK_K/4;

        for (int ib32 = 0; ib32 < QK_K/32; ++ib32) {
            memcpy(&aux32, scales_and_signs + 4*ib32, sizeof(uint32_t));
            const float db = d * (0.5f + (aux32 >> 28)) * 0.5f;
            for (int l = 0; l < 4; ++l) {
                const uint8_t  signs = ksigns_iq2xs[(aux32 >> 7*l) & 127];
                const uint8_t * grid1 = (const uint8_t *)(iq3xxs_grid + qs[2*l+0]);
                const uint8_t * grid2 = (const uint8_t *)(iq3xxs_grid + qs[2*l+1]);
                for (int j = 0; j < 4; ++j) {
                    y[j+0] = db * grid1[j] * (signs & kmask_iq2xs[j+0] ? -1.f : 1.f);
                    y[j+4] = db * grid2[j] * (signs & kmask_iq2xs[j+4] ? -1.f : 1.f);
                }
                y += 8;
            }
            qs += 8;
        }
    }
}
static uint32_t f2b(float f){union{float f;uint32_t u;}v;v.f=f;return v.u;}
#define NB 16
#define K (NB*256)
static float w[K], a[K];
static uint8_t raw[NB*128];
static int run(const char*path,int bsz,const char*name,int which){
    FILE*fp=fopen(path,"rb"); uint32_t h=2166136261u; int i,k; float acc;
    size_t n=fread(raw,1,(size_t)NB*bsz,fp); fclose(fp);
    if(n!=(size_t)NB*bsz){ printf("/* short read %s */\n",path); return 1; }
    if(which==0) dequantize_row_iq1_m((const block_iq1_m*)raw,w,K);
    if(which==1) dequantize_row_iq2_xxs((const block_iq2_xxs*)raw,w,K);
    if(which==2) dequantize_row_iq3_xxs((const block_iq3_xxs*)raw,w,K);
    for(i=0;i<K;i++){uint32_t u=f2b(w[i]);for(k=0;k<4;k++){h^=(u>>(8*k))&0xFF;h*=16777619u;}}
    for(i=0;i<K;i++) a[i]=(float)((i%29)-14)*0.015625f;
    acc=0.0f;
    for(i=0;i<NB;i++){ float t=0.0f; int j;
        for(j=0;j<256;j++) t = t + (float)(w[i*256+j]*a[i*256+j]);
        acc = acc + t; }
    printf("#define %s_HASH 0x%08Xu\n",name,h);
    printf("#define %s_DOT  0x%08Xu\n",name,f2b(acc));
    printf("/* %s w[0]=%g w[K-1]=%g dot=%.9g */\n",name,w[0],w[K-1],acc);
    printf("static const unsigned char %s_blk[%d] = {\n",name,NB*bsz);
    for(i=0;i<NB*bsz;i++){printf("%u%s",raw[i],i+1<NB*bsz?",":"");if(i%20==19)printf("\n");}
    printf("};\n");
    return 0;
}
int main(void){
    printf("#define IQ_NB %d\n",NB);
    run("/tmp/iq1m.bin",  56,"IQ1M",0);
    run("/tmp/iq2xxs.bin",66,"IQ2XXS",1);
    run("/tmp/iq3xxs.bin",98,"IQ3XXS",2);
    return 0;
}
