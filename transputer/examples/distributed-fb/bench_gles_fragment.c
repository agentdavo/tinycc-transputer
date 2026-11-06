/* T4_EST_CYCLES=1 makes ldtimer return an estimated T800 cycle count. */
#include "gles2_fragment.h"
#define UART (*(volatile unsigned int *)0x10000000u)
unsigned int __ldtimer(void);
static const unsigned char pixel[4]={128,128,255,255};
static tg_fragment_texture texture={pixel,1,1,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
static tg_fragment_material material; static tg_fragment_varyings varyings;
static void print(const char *s) { while(*s) UART=(unsigned char)*s++; }
static void hex(unsigned int n) { int i; for(i=28;i>=0;i-=4) UART="0123456789abcdef"[(n>>i)&15]; }
static float square(float x,float exponent) { (void)exponent; return x*x; }
int test_main(void)
{
    unsigned int start, delta, hash=0, bad=0; int i, mode; float out[4];
    material.base=material.bump=material.specular=material.shadow=&texture;
    material.ambient[0]=material.ambient[1]=material.ambient[2]=0.2f;
    material.diffuse[0]=material.diffuse[1]=material.diffuse[2]=0.5f;
    material.specular_light[0]=material.specular_light[1]=material.specular_light[2]=0.2f;
    material.specular_material[0]=material.specular_material[1]=material.specular_material[2]=0.3f;
    material.shininess=2.0f; material.pow_fn=square;
    varyings.normal[2]=varyings.light[2]=varyings.half_vector[2]=1.0f; varyings.light_clip[3]=1.0f;
    for(mode=0;mode<2;mode++) {
        material.features=mode ? (TG_FRAG_BUMP|TG_FRAG_SPEC|TG_FRAG_SHADOW) : 0;
        hash=2166136261u;
        start=__ldtimer();
        for(i=0;i<1024;i++) {
            varyings.uv[0]=(float)(i&1)*0.5f;
            bad+=(unsigned int)(tg_fragment_uber(&material,&varyings,out)!=TG_FRAGMENT_OK);
            hash=(hash^tg_fragment_argb(out))*16777619u;
        }
        delta=__ldtimer()-start;
        print(mode ? "FRAGBENCH full " : "FRAGBENCH base "); hex(delta); UART=' '; hex(hash); UART=' '; hex(bad); UART='\n';
    }
    return 0;
}
