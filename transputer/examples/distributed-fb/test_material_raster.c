#include "material_raster.h"
#include "shadow_map.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static unsigned int pixels[TG_TILE_W*TG_TILE_H];
static int depth[TG_TILE_W*TG_TILE_H];
static float shadow_depth[64]; static unsigned char shadow_rgba[64*4];
static const unsigned char texels[]={255,0,0,255, 0,255,0,255};
static float square(float x,float y) { (void)y; return x*x; }
static int run(void)
{
    static tg_context c; static tg_material_vertex a,b,d;
    static tg_fragment_material m; static tg_fragment_texture t;
    int i;
    c.color=pixels; c.depth=depth; c.tile_x=c.tile_y=0;
    for(i=0;i<TG_TILE_W*TG_TILE_H;i++) { pixels[i]=0; depth[i]=-2147483647; }
    t.rgba=texels; t.width=2; t.height=1; t.wrap_s=TG_FRAG_CLAMP; t.wrap_t=TG_FRAG_CLAMP;
    m.base=&t; m.ambient[0]=m.ambient[1]=m.ambient[2]=1; m.pow_fn=square; m.shininess=2;
    a.x=0; a.y=0; b.x=0; b.y=8; d.x=8; d.y=0;
    a.depth=b.depth=d.depth=42; a.inverse_w=d.inverse_w=1; b.inverse_w=4;
    a.varying.normal[2]=b.varying.normal[2]=d.varying.normal[2]=1;
    a.varying.half_vector[2]=b.varying.half_vector[2]=d.varying.half_vector[2]=1;
    b.varying.uv[0]=1;
    if(tg_material_triangle(&c,&a,&b,&d,&m)) return 1;
    /* Affine u is .25 here, perspective-correct u is > .5. */
    if(pixels[2*TG_TILE_W+2]!=0xff00ff00u || depth[2*TG_TILE_W+2]!=42) return 2;
    if(pixels[0]!=0xffff0000u || pixels[9*TG_TILE_W+9]!=0) return 3;
    c.clip_enabled=1; c.clip_x0=4; c.clip_x1=8; c.clip_y0=0; c.clip_y1=8;
    a.depth=b.depth=d.depth=43;
    if(tg_material_triangle(&c,&a,&b,&d,&m) || depth[2*TG_TILE_W+2]!=42 || depth[4]!=43) return 4;
    {
        tg_shadow_map sm={8,8,shadow_depth,shadow_rgba};
        tg_shadow_vertex s0={0,0,0.25f},s1={0,7,0.25f},s2={7,0,0.25f};
        tg_fragment_texture st={shadow_rgba,8,8,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
        if(!tg_shadow_clear(&sm)) return 5;
        tg_shadow_triangle(&sm,&s0,&s1,&s2);
        if(shadow_depth[2*8+2]!=0.25f || shadow_depth[7*8+7]!=1.0f) return 6;
        m.shadow=&st; m.features=TG_FRAG_SHADOW;
        m.ambient[0]=m.ambient[1]=m.ambient[2]=0;
        m.diffuse[0]=m.diffuse[1]=m.diffuse[2]=1;
        a.varying.light[2]=b.varying.light[2]=d.varying.light[2]=1;
        a.varying.light_clip[0]=b.varying.light_clip[0]=d.varying.light_clip[0]=-0.5f;
        a.varying.light_clip[1]=b.varying.light_clip[1]=d.varying.light_clip[1]=-0.5f;
        a.varying.light_clip[2]=b.varying.light_clip[2]=d.varying.light_clip[2]=0.0f;
        a.varying.light_clip[3]=b.varying.light_clip[3]=d.varying.light_clip[3]=1.0f;
        c.clip_enabled=0; a.depth=b.depth=d.depth=44;
        if(tg_material_triangle(&c,&a,&b,&d,&m) || pixels[2*TG_TILE_W+2]!=0xff000000u) return 7;
        a.varying.light_clip[0]=b.varying.light_clip[0]=d.varying.light_clip[0]=0.9f;
        a.varying.light_clip[1]=b.varying.light_clip[1]=d.varying.light_clip[1]=0.9f;
        a.depth=b.depth=d.depth=45;
        if(tg_material_triangle(&c,&a,&b,&d,&m) || pixels[2*TG_TILE_W+2]!=0xff00ff00u) return 8;
    }
    {
        static const unsigned char translucent[4]={255,0,0,128};
        t.rgba=translucent; t.width=t.height=1; m.features=0;
        m.ambient[0]=m.ambient[1]=m.ambient[2]=1;
        pixels[0]=0xff0000ffu; depth[0]=50; a.depth=b.depth=d.depth=51;
        if(tg_material_triangle_blended(&c,&a,&b,&d,&m) || pixels[0]!=0xff80007fu || depth[0]!=50) return 9;
        a.depth=b.depth=d.depth=49;
        if(tg_material_triangle_blended(&c,&a,&b,&d,&m) || pixels[0]!=0xff80007fu || depth[0]!=50) return 10;
    }
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void) { volatile unsigned int *u=(volatile unsigned int *)0x10000000u; int n=run(); const char *p=n?"MATERIAL BAD\n":"MATERIAL OK\n"; while(*p)*u=*p++; return n; }
#else
int main(void) { int n=run(); if(!n) puts("material raster host tests OK"); return n; }
#endif
