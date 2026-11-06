#include "glsl_fragment_compiler.h"
#ifndef __transputer__
#include <math.h>
static float root(float x){return sqrtf(x);}
#else
static float root(float x){return __fpsqrt(x);}
#endif
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static tg_glsl_fragment_program p; static tg_vm_state s;
static const unsigned char pixels[]={255,0,0,255,0,128,255,128};
static const tg_fragment_texture texture={pixels,2,1,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
static int close_to(float a,float b) { float d=a-b;return d<0.0001f&&d>-0.0001f; }
static int run(void)
{
    const char *source="precision mediump float; varying vec2 uv; uniform sampler2D tex; uniform vec4 tint; void main(){gl_FragColor=texture2D(tex,uv)*tint+0.1;}";
    int uv,tint,tex,i; float *out;
    if(!tg_glsl_compile_fragment(source,&p))return 1;
    uv=tg_glsl_find_symbol(&p,"uv");tint=tg_glsl_find_symbol(&p,"tint");tex=tg_glsl_find_symbol(&p,"tex");
    if(uv<0||tint<0||tex<0||p.symbols[uv].kind!=TG_GLSL_VARYING||p.symbols[tex].kind!=TG_GLSL_SAMPLER)return 2;
    tg_glsl_fragment_init(&p,&s);s.reg[p.symbols[uv].slot][0]=0.75f;
    for(i=0;i<4;i++)s.reg[p.symbols[tint].slot][i]=0.5f;
    s.texture[p.symbols[tex].slot]=&texture;
    if(tg_glsl_vm_run(&s,p.code,p.instruction_count)!=TG_VM_OK)return 3;
    out=s.reg[p.output_register];
    if(!close_to(out[0],0.1f)||!close_to(out[1],64.0f/255.0f+0.1f)||!close_to(out[2],0.6f)||!close_to(out[3],64.0f/255.0f+0.1f))return 4;
    s.reg[p.symbols[uv].slot][0]=0;
    if(tg_glsl_vm_run(&s,p.code,p.instruction_count)!=TG_VM_OK||!close_to(out[0],0.6f)||!close_to(out[1],0.1f))return 5;
    if(tg_glsl_compile_fragment("uniform sampler2D x; varying vec3 uv; void main(){gl_FragColor=texture2D(x,uv);}",&p))return 6;
    if(tg_glsl_compile_fragment("varying vec4 x; void main(){gl_FragColor=unknown(x);}",&p))return 7;
    if(tg_glsl_compile_fragment("varying vec4 x; void main(){gl_FragColor=x;} garbage",&p))return 8;
    if(!tg_glsl_compile_fragment("varying vec3 n; uniform vec3 l; uniform vec4 color; void main(){gl_FragColor=color*max(dot(normalize(n),l),0.0);}",&p))return 9;
    tg_glsl_fragment_init(&p,&s);s.sqrt_fn=root;
    s.reg[p.symbols[tg_glsl_find_symbol(&p,"n")].slot][0]=3;s.reg[p.symbols[tg_glsl_find_symbol(&p,"n")].slot][1]=4;
    s.reg[p.symbols[tg_glsl_find_symbol(&p,"l")].slot][0]=1;
    for(i=0;i<4;i++)s.reg[p.symbols[tg_glsl_find_symbol(&p,"color")].slot][i]=1;
    if(tg_glsl_vm_run(&s,p.code,p.instruction_count)!=TG_VM_OK||!close_to(s.reg[p.output_register][0],0.6f))return 10;
    if(!tg_glsl_compile_fragment("varying vec2 uv; uniform sampler2D tex; uniform vec4 tint; void main(){gl_FragColor=vec4(texture2D(tex,uv).rgb,tint.a);}",&p))return 11;
    tg_glsl_fragment_init(&p,&s);s.texture[p.symbols[tg_glsl_find_symbol(&p,"tex")].slot]=&texture;
    s.reg[p.symbols[tg_glsl_find_symbol(&p,"uv")].slot][0]=0.75f;s.reg[p.symbols[tg_glsl_find_symbol(&p,"tint")].slot][3]=0.25f;
    if(tg_glsl_vm_run(&s,p.code,p.instruction_count)!=TG_VM_OK||!close_to(s.reg[p.output_register][1],128.0f/255.0f)||!close_to(s.reg[p.output_register][3],0.25f))return 12;
    if(tg_glsl_compile_fragment("varying vec4 x; void main(){gl_FragColor=x.xgba;}",&p))return 13;
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void) { volatile unsigned int *u=(volatile unsigned int *)0x10000000u;int n=run();const char *p=n?"GLSL SOURCE BAD\n":"GLSL SOURCE OK\n";while(*p)*u=*p++;return n; }
#else
int main(void) { int n=run();if(!n)puts("GLSL source fragment host tests OK");return n; }
#endif
