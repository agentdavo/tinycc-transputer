#include "glsl_vm.h"
#ifndef __transputer__
#include <math.h>
static float root(float x) { return sqrtf(x); }
#else
static float root(float x) { return __fpsqrt(x); }
#endif
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static float square(float x,float y) { (void)y; return x*x; }
static int close_to(float a,float b) { float d=a-b; return d<0.0001f && d>-0.0001f; }
static int run(void)
{
    static tg_vm_state s; static const unsigned char pixels[]={255,0,0,255, 0,128,255,128};
    static const tg_fragment_texture tex={pixels,2,1,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
    static const tg_vm_instruction program[]={
        {TG_VM_TEXTURE2D,2,0,0,4,0,0}, /* texture2D(tex, uv) */
        {TG_VM_MUL,3,2,1,4,0,0},      /* multiply uniform vec4 */
        {TG_VM_SWIZZLE,4,3,0,3,0,0x1b}, /* .abg */
        {TG_VM_DOT,5,6,6,3,0,0},
        {TG_VM_NORMALIZE,7,6,0,3,0,0},
        {TG_VM_CROSS,8,9,10,3,0,0},
        {TG_VM_MAT4_VEC4,11,12,16,4,0,0},
        {TG_VM_POW,17,18,19,1,0,0},
        {TG_VM_MAX,20,18,21,3,2,0}
    };
    tg_vm_instruction bad={TG_VM_TEXTURE2D,2,0,0,4,0,8};
    int i; s.sqrt_fn=root; s.pow_fn=square; s.texture[0]=&tex;
    s.reg[0][0]=0.75f; for(i=0;i<4;i++)s.reg[1][i]=0.5f;
    s.reg[6][0]=3; s.reg[6][1]=4; s.reg[9][0]=1; s.reg[10][1]=1;
    for(i=0;i<4;i++)s.reg[12+i][i]=1; s.reg[15][0]=2; s.reg[16][3]=1;
    s.reg[18][0]=0.5f; s.reg[18][1]=-1; s.reg[18][2]=2; s.reg[19][0]=2; s.reg[21][0]=0;
    if(tg_glsl_vm_run(&s,program,sizeof(program)/sizeof(program[0]))!=TG_VM_OK)return 1;
    if(!close_to(s.reg[3][1],64.0f/255.0f) || !close_to(s.reg[3][2],0.5f) || !close_to(s.reg[3][3],64.0f/255.0f))return 2;
    if(!close_to(s.reg[4][0],64.0f/255.0f) || !close_to(s.reg[4][1],0.5f))return 3;
    if(!close_to(s.reg[5][0],25) || !close_to(s.reg[7][0],0.6f) || !close_to(s.reg[7][1],0.8f) || !close_to(s.reg[8][2],1))return 4;
    if(!close_to(s.reg[11][0],2) || !close_to(s.reg[11][3],1) || !close_to(s.reg[17][0],0.25f) || s.reg[20][1]!=0)return 5;
    if(tg_glsl_vm_run(&s,&bad,1)!=TG_VM_INVALID)return 6;
    bad.op=TG_VM_DIV; bad.aux=0; bad.a=18; bad.b=21; bad.width=1;
    if(tg_glsl_vm_run(&s,&bad,1)!=TG_VM_UNDEFINED)return 7;
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void) { volatile unsigned int *u=(volatile unsigned int *)0x10000000u; int n=run(); const char *p=n?"GLSL VM BAD\n":"GLSL VM OK\n"; while(*p)*u=*p++; return n; }
#else
int main(void) { int n=run(); if(!n) puts("GLSL VM host tests OK"); return n; }
#endif
