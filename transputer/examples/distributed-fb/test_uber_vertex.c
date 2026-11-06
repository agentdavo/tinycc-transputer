#include "gles2_uber_vertex.h"
#ifndef __transputer__
#include <math.h>
static float root(float x) { return sqrtf(x); }
#else
static float root(float x) { return __fpsqrt(x); }
#endif
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static int near(float a,float b) { float d=a-b; return d<0.0002f && d>-0.0002f; }
static int run(void)
{
    static tg_uber_vertex_input in; static tg_uber_vertex_uniforms u; static tg_uber_vertex_output out;
    int i;
    for(i=0;i<4;i++) { u.mvp[i*5]=1; u.light_mvp[i*5]=1; }
    u.mvp[12]=2; u.light_mvp[13]=3; u.sqrt_fn=root;
    in.normal[2]=1; in.tangent[0]=1; in.uv[0]=0.25f; in.uv[1]=0.75f;
    u.light_position[0]=3; u.light_position[2]=4; u.camera_position[2]=5;
    u.features=TG_FRAG_BUMP|TG_FRAG_SHADOW;
    if(tg_uber_vertex(&in,&u,&out)!=TG_FRAGMENT_OK) return 1;
    if(!near(out.clip[0],2) || !near(out.clip[3],1) || !near(out.varying.light_clip[1],3)) return 2;
    if(!near(out.varying.light[0],0.6f) || !near(out.varying.light[2],0.8f) ||
       !near(out.varying.half_vector[0],0.3162278f) || !near(out.varying.half_vector[2],0.9486833f)) return 3;
    if(!near(out.varying.uv[0],0.25f) || !near(out.varying.uv[1],0.75f)) return 4;
    u.features=0;
    if(tg_uber_vertex(&in,&u,&out)!=TG_FRAGMENT_OK || !near(out.varying.normal[2],1) ||
       !(out.varying.half_vector[0]>0.102f && out.varying.half_vector[0]<0.104f)) return 5;
    u.light_position[0]=u.light_position[2]=0; out.clip[0]=123;
    if(tg_uber_vertex(&in,&u,&out)!=TG_FRAGMENT_UNDEFINED || out.clip[0]!=123) return 6;
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void) { volatile unsigned int *u=(volatile unsigned int *)0x10000000u; int n=run(); const char *p=n?"VERTEX UBER BAD\n":"VERTEX UBER OK\n"; while(*p)*u=*p++; return n; }
#else
int main(void) { int n=run(); if(!n) puts("uber vertex host tests OK"); return n; }
#endif
