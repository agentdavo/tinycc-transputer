#include "demo_room.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static unsigned int calls, invalid;
static unsigned int shadow_calls;
void tg_shadow_triangle(tg_shadow_map *m,const tg_shadow_vertex *a,const tg_shadow_vertex *b,const tg_shadow_vertex *c)
{ (void)m; (void)a; (void)b; (void)c; shadow_calls++; }
int tg_material_triangle(tg_context *c,const tg_material_vertex *a,const tg_material_vertex *b,
                         const tg_material_vertex *d,const tg_fragment_material *m)
{
    const tg_material_vertex *v[3]; int i; (void)c; v[0]=a; v[1]=b; v[2]=d; calls++;
    if(!m->base || !m->base->rgba || m->base->width!=16 || m->base->height!=16) invalid++;
    for(i=0;i<3;i++) if(v[i]->inverse_w<=0.0f || v[i]->inverse_w>5.01f || v[i]->depth<=0) invalid++;
    return 0;
}
int tg_material_triangle_blended(tg_context *c,const tg_material_vertex *a,const tg_material_vertex *b,
                                 const tg_material_vertex *d,const tg_fragment_material *m)
{ return tg_material_triangle(c,a,b,d,m); }
static int run(void)
{
    static tg_context c; static const unsigned char texel[4]={255,255,255,255};
    tg_fragment_texture shadow={texel,1,1,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
    demo_room_init(0,0,0,0.01f,0,0,1);
    { tg_shadow_map sm={0}; demo_room_shadow(&sm); if(shadow_calls==0 || shadow_calls>=653) return 3; }
    if(demo_room_draw(&c,&shadow,0) || invalid || calls<100 || calls>1306) return 1;
    { unsigned int opaque=calls; if(demo_room_draw(&c,&shadow,1) || calls<=opaque || invalid) return 2; }
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void) { volatile unsigned int *u=(volatile unsigned int *)0x10000000u; int n=run(); const char *p=n?"ROOM BAD\n":"ROOM OK\n"; while(*p)*u=*p++; return n; }
#else
int main(void) { int n=run(); if(!n) puts("room clipping host tests OK"); return n; }
#endif
