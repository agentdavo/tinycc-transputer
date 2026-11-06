#include "gles2_fragment.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static int pow_calls;
static float square(float x,float exponent) { pow_calls++; return exponent==2.0f ? x*x : 0.0f; }
static int close_to(float a,float b) { float d=a-b; return d<0.0001f && d>-0.0001f; }
static int run(void)
{
    static const unsigned char pixels[16]={255,0,0,255, 0,255,0,128, 0,0,255,64, 128,64,32,200};
    static const unsigned char base_pixels[4]={128,64,32,200};
    static const unsigned char bump_pixels[4]={128,128,255,255};
    static const unsigned char spec_pixels[4]={64,128,255,255};
    static const unsigned char dark_pixels[4]={0,0,0,0};
    static const unsigned char lit_pixels[4]={0,0,0,255};
    tg_fragment_texture grid={pixels,2,2,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
    tg_fragment_texture base={base_pixels,1,1,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
    tg_fragment_texture bump={bump_pixels,1,1,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
    tg_fragment_texture spec={spec_pixels,1,1,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
    tg_fragment_texture shadow={dark_pixels,1,1,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
    static tg_fragment_material m; static tg_fragment_varyings v; float out[4], r,g,b; int flags;
    if (tg_fragment_sample(&grid,-1.0f,2.0f,out)!=TG_FRAGMENT_OK ||
        out[0]!=0.0f || out[1]!=0.0f || out[2]!=1.0f || !close_to(out[3],64.0f/255.0f)) return 1;
    grid.wrap_s=TG_FRAG_REPEAT; grid.wrap_t=TG_FRAG_REPEAT;
    if (tg_fragment_sample(&grid,-0.25f,1.25f,out)!=TG_FRAGMENT_OK ||
        out[0]!=0.0f || out[1]!=1.0f || out[2]!=0.0f) return 2;
    m.base=&base; m.bump=&bump; m.specular=&spec; m.shadow=&shadow;
    m.ambient[0]=m.ambient[1]=m.ambient[2]=0.25f;
    m.diffuse[0]=m.diffuse[1]=m.diffuse[2]=0.5f;
    m.specular_light[0]=m.specular_light[1]=m.specular_light[2]=0.2f;
    m.specular_material[0]=m.specular_material[1]=m.specular_material[2]=0.5f;
    m.shininess=2.0f; m.pow_fn=square;
    v.normal[2]=v.light[2]=v.half_vector[2]=1.0f; v.light_clip[3]=1.0f;
    if (tg_fragment_uber(&m,&v,out)!=TG_FRAGMENT_OK || pow_calls!=1) return 3;
    r=(128.0f/255.0f)*0.75f+0.1f; g=(64.0f/255.0f)*0.75f+0.1f; b=(32.0f/255.0f)*0.75f+0.1f;
    if(!close_to(out[0],r) || !close_to(out[1],g) || !close_to(out[2],b) || !close_to(out[3],200.0f/255.0f)) return 4;
    m.features=TG_FRAG_BUMP|TG_FRAG_SPEC;
    if(tg_fragment_uber(&m,&v,out)!=TG_FRAGMENT_OK ||
       !close_to(out[0],(128.0f/255.0f)*0.75f+0.2f*(64.0f/255.0f)) ||
       !close_to(out[1],(64.0f/255.0f)*0.75f+0.2f*(128.0f/255.0f)) ||
       !close_to(out[2],(32.0f/255.0f)*0.75f+0.2f)) return 5;
    m.features|=TG_FRAG_SHADOW;
    if(tg_fragment_uber(&m,&v,out)!=TG_FRAGMENT_OK ||
       !close_to(out[0],(128.0f/255.0f)*0.25f) || !close_to(out[3],200.0f/255.0f)) return 6;
    shadow.rgba=lit_pixels;
    if(tg_fragment_uber(&m,&v,out)!=TG_FRAGMENT_OK ||
       !close_to(out[0],(128.0f/255.0f)*0.75f+0.2f*(64.0f/255.0f))) return 7;
    for(flags=0;flags<8;flags++) {
        m.features=(unsigned int)flags;
        r=(128.0f/255.0f)*0.75f+0.2f*((flags&TG_FRAG_SPEC) ? (64.0f/255.0f) : 0.5f);
        if(tg_fragment_uber(&m,&v,out)!=TG_FRAGMENT_OK || !close_to(out[0],r)) return 12;
    }
    m.features=TG_FRAG_SHADOW; shadow.rgba=dark_pixels; v.light_clip[2]=-1.0f;
    if(tg_fragment_uber(&m,&v,out)!=TG_FRAGMENT_OK || !close_to(out[0],(128.0f/255.0f)*0.75f+0.1f)) return 13;
    v.light_clip[2]=0.0f;
    v.light_clip[3]=0.0f; out[0]=17.0f;
    if(tg_fragment_uber(&m,&v,out)!=TG_FRAGMENT_UNDEFINED || out[0]!=17.0f) return 8;
    m.features=0; v.normal[2]=-1.0f; out[0]=17.0f;
    if(tg_fragment_uber(&m,&v,out)!=TG_FRAGMENT_UNDEFINED || out[0]!=17.0f) return 9;
    m.clamp_specular_base=1;
    if(tg_fragment_uber(&m,&v,out)!=TG_FRAGMENT_OK) return 14;
    m.clamp_specular_base=0;
    if(tg_fragment_sample(&grid,40000.0f,0.0f,out)!=TG_FRAGMENT_INVALID) return 10;
    out[0]=2.0f; out[1]=-1.0f; out[2]=0.5f; out[3]=1.0f;
    if(tg_fragment_argb(out)!=0xffff0080u) return 11;
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void)
{
    volatile unsigned int *uart=(volatile unsigned int *)0x10000000u; int n=run();
    const char *msg=n ? "FRAGMENT BAD " : "FRAGMENT OK\n";
    while(*msg) *uart=(unsigned char)*msg++;
    if(n) { *uart="0123456789ABCDEF"[n]; *uart='\n'; }
    return 0;
}
#else
int main(void) { int n=run(); if(!n) puts("fragment host tests OK"); return n; }
#endif
