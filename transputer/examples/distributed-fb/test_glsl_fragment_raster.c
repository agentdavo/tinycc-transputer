#include "material_raster.h"
#include "glsl_fragment_shader.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static unsigned int tile[TG_TILE_W*TG_TILE_H];static int depth[TG_TILE_W*TG_TILE_H];
static tg_glsl_fragment_program program;static tg_glsl_fragment_shader shader;
static tg_context c;static tg_material_vertex a,b,d;
static int run(void)
{
    static const unsigned char pixels[]={255,0,0,255,0,255,0,255};
    static const tg_fragment_texture tex={pixels,2,1,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
    static const float tint[]={0.5f,0.5f,0.5f,1};int i;
    const char *source="varying vec2 uv; uniform sampler2D map; uniform vec4 tint; void main(){gl_FragColor=texture2D(map,uv)*tint;}";
    if(!tg_glsl_compile_fragment(source,&program))return 1;
    tg_glsl_shader_init(&shader,&program);
    if(!tg_glsl_shader_varying(&shader,"uv",TG_GLSL_INPUT_UV)||!tg_glsl_shader_uniform(&shader,"tint",tint,4)||!tg_glsl_shader_texture(&shader,"map",&tex))return 2;
    if(tg_glsl_shader_varying(&shader,"uv",TG_GLSL_INPUT_NORMAL)||tg_glsl_shader_uniform(&shader,"tint",tint,3))return 3;
    c.color=tile;c.depth=depth;for(i=0;i<TG_TILE_W*TG_TILE_H;i++){tile[i]=0;depth[i]=-2147483647;}
    a.x=0;a.y=0;b.x=0;b.y=8;d.x=8;d.y=0;a.depth=b.depth=d.depth=42;
    a.inverse_w=d.inverse_w=1;b.inverse_w=4;b.varying.uv[0]=1;
    if(tg_material_triangle_custom(&c,&a,&b,&d,tg_glsl_shader_fragment,&shader))return 4;
    if(tile[2*TG_TILE_W+2]!=0xff008000u || tile[0]!=0xff800000u || depth[2*TG_TILE_W+2]!=42)return 5;
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void){volatile unsigned int *u=(volatile unsigned int *)0x10000000u;int n=run();const char *p=n?"GLSL RASTER BAD\n":"GLSL RASTER OK\n";while(*p)*u=*p++;return n;}
#else
int main(void){int n=run();if(!n)puts("GLSL fragment raster host tests OK");return n;}
#endif
