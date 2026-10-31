/* Host reference for Q8_0 using llama.cpp's exact semantics. */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
static uint32_t f2b(float f){union{float f;uint32_t u;}v;v.f=f;return v.u;}
/* GGML_FP16_TO_FP32, by definition */
static float f16(uint16_t h){
    int s=(h>>15)&1,e=(h>>10)&0x1F,m=h&0x3FF; double v;
    if(e==0) v=ldexp((double)m,-24);
    else if(e==31) return s?-INFINITY:INFINITY;
    else v=ldexp(1024.0+(double)m,e-25);
    return (float)(s?-v:v);
}
#define NB 128
#define K (NB*32)
int main(void){
    static uint8_t blk[NB*34]; static float w[K],a[K];
    FILE*fp=fopen("/tmp/q8.bin","rb"); uint32_t h=2166136261u; int i,b,j,k; float acc;
    if(fread(blk,1,sizeof blk,fp)!=sizeof blk){printf("short read\n");return 1;} fclose(fp);
    /* llama.cpp dequantize_row_q8_0: y[i*32+j] = qs[j] * d */
    for(b=0;b<NB;b++){
        uint16_t hd = (uint16_t)(blk[b*34] | (blk[b*34+1]<<8));
        float d=f16(hd);
        for(j=0;j<32;j++) w[b*32+j]=(float)(int8_t)blk[b*34+2+j]*d;
    }
    for(i=0;i<K;i++){uint32_t u=f2b(w[i]);for(k=0;k<4;k++){h^=(u>>(8*k))&0xFF;h*=16777619u;}}
    for(i=0;i<K;i++) a[i]=(float)((i%23)-11)*0.03125f;
    /* Q8DOT order: per block sequential f32, delta applied once at the end */
    acc=0.0f;
    for(b=0;b<NB;b++){
        uint16_t hd=(uint16_t)(blk[b*34]|(blk[b*34+1]<<8));
        float t=0.0f,d=f16(hd);
        for(j=0;j<32;j++) t = t + (float)(int8_t)blk[b*34+2+j]*a[b*32+j];
        acc = acc + d*t;
    }
    printf("#define Q8_NB %d\n",NB);
    printf("#define Q8_HASH 0x%08Xu\n",h);
    printf("#define Q8_DOT  0x%08Xu\n",f2b(acc));
    printf("/* d[0]=%g w[0]=%g w[4095]=%g dot=%.9g */\n",f16((uint16_t)(blk[0]|(blk[1]<<8))),w[0],w[K-1],acc);
    printf("static const unsigned char q8blk[%d] = {\n",NB*34);
    for(i=0;i<NB*34;i++){printf("%u%s",blk[i],i+1<NB*34?",":"");if(i%20==19)printf("\n");}
    printf("};\n");
    return 0;
}
