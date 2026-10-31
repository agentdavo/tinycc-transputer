#include <stdio.h>
#include <math.h>
#include <stdint.h>
static uint32_t f2b(float f){union{float f;uint32_t u;}v;v.f=f;return v.u;}
#define N 16
int main(void){
    float x[N], g[N], y[N];
    int i;
    for(i=0;i<N;i++){ x[i]=((float)(i-8))*0.375f; g[i]=1.0f+0.125f*(float)i; y[i]=0.25f*(float)(i+1); }
    printf("#define VN %d\n", N);
    printf("static const unsigned int vx[VN]={"); for(i=0;i<N;i++) printf("0x%08Xu%s",f2b(x[i]),i<N-1?",":""); printf("};\n");
    printf("static const unsigned int vg[VN]={"); for(i=0;i<N;i++) printf("0x%08Xu%s",f2b(g[i]),i<N-1?",":""); printf("};\n");
    printf("static const unsigned int vy[VN]={"); for(i=0;i<N;i++) printf("0x%08Xu%s",f2b(y[i]),i<N-1?",":""); printf("};\n");
    /* add / mul */
    printf("static const unsigned int e_add[VN]={"); for(i=0;i<N;i++) printf("0x%08Xu%s",f2b((float)((double)x[i]+(double)y[i])),i<N-1?",":""); printf("};\n");
    printf("static const unsigned int e_mul[VN]={"); for(i=0;i<N;i++) printf("0x%08Xu%s",f2b((float)((double)x[i]*(double)y[i])),i<N-1?",":""); printf("};\n");
    /* silu */
    printf("static const unsigned int e_silu[VN]={"); for(i=0;i<N;i++){ double v=(double)x[i]; printf("0x%08Xu%s",f2b((float)(v/(1.0+exp(-v)))),i<N-1?",":""); } printf("};\n");
    /* rmsnorm */
    { double ss=0.0,r; for(i=0;i<N;i++){double v=(double)x[i]; ss+=v*v;} r=1.0/sqrt(ss/(double)N+1e-6);
      printf("static const unsigned int e_rms[VN]={"); for(i=0;i<N;i++) printf("0x%08Xu%s",f2b((float)((double)x[i]*r*(double)g[i])),i<N-1?",":""); printf("};\n"); }
    /* softmax */
    { double mx=(double)x[0],sum=0.0; for(i=1;i<N;i++){double v=(double)x[i]; if(v>mx)mx=v;}
      for(i=0;i<N;i++) sum+=exp((double)x[i]-mx);
      printf("static const unsigned int e_sm[VN]={"); for(i=0;i<N;i++) printf("0x%08Xu%s",f2b((float)(exp((double)x[i]-mx)/sum)),i<N-1?",":""); printf("};\n"); }
    return 0;
}
