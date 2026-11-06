/* Pre-skinned idle preview using the material rasterizer and a per-node shadow pass.
 * This is a native C material path, not the original dEngine GLSL renderer. */
#include "../distributed-fb/material_raster.h"
#include "../distributed-fb/shadow_map.h"
#include "hellknight_asset.h"
static const int sine[32]={0,50,98,142,181,213,237,251,256,251,237,213,181,142,98,50,0,-50,-98,-142,-181,-213,-237,-251,-256,-251,-237,-213,-181,-142,-98,-50};
static tg_material_vertex verts[HK_VERTEX_COUNT];
static tg_shadow_vertex light_verts[HK_VERTEX_COUNT];
static float map_depth[64*64]; static unsigned char map_pixels[64*64*4];
static tg_shadow_map map={64,64,map_depth,map_pixels};
static tg_fragment_texture base={hk_diffuse,32,32,TG_FRAG_REPEAT,TG_FRAG_REPEAT};
static tg_fragment_texture shadow={map_pixels,64,64,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
static tg_fragment_material material;
static unsigned int previous_frame=~0u;
static float square(float x,float exponent) { (void)exponent; return x*x; }
static void prepare(unsigned int frame)
{
    unsigned int i, pose=(frame/15)%HK_ANIMATION_FRAMES, next=(pose+1)%HK_ANIMATION_FRAMES;
    float blend=(float)(frame%15)/15.0f,sy=sine[frame&31]/256.0f,cy=sine[(frame+8)&31]/256.0f;
#ifdef TG_DISABLE_ROTATION
    sy=0; cy=1;
#endif
    material.base=&base; material.shadow=&shadow;
#ifdef TG_DISABLE_SHADOW
    material.features=0;
#else
    material.features=TG_FRAG_SHADOW;
#endif
    material.pow_fn=square; material.shininess=2;
    for(i=0;i<3;i++) { material.ambient[i]=0.35f; material.diffuse[i]=0.75f; }
    for(i=0;i<HK_VERTEX_COUNT;i++) {
        const short *position=hk_animated[pose][i], *position_next=hk_animated[next][i];
        const short *normal=hk_animated_normals[pose][i], *normal_next=hk_animated_normals[next][i];
        float x,y,z,nx,ny,nz;
#ifdef TG_DISABLE_ANIMATION
        position=position_next=hk_vertices[i]; normal=normal_next=hk_normals[i];
#endif
        x=(position[0]+(position_next[0]-position[0])*blend)/32767.0f;
        y=(position[1]+(position_next[1]-position[1])*blend)/32767.0f;
        z=(position[2]+(position_next[2]-position[2])*blend)/32767.0f;
        float rx=cy*x+sy*z,rz=-sy*x+cy*z,w=3.0f-rz;
        nx=(normal[0]+(normal_next[0]-normal[0])*blend)/32767.0f;
        ny=(normal[1]+(normal_next[1]-normal[1])*blend)/32767.0f;
        nz=(normal[2]+(normal_next[2]-normal[2])*blend)/32767.0f;
        tg_material_vertex *v=&verts[i];
        v->x=(int)(80.0f+110.0f*rx/w); v->y=(int)(60.0f-110.0f*y/w);
        v->depth=(int)(100000.0f/w); v->inverse_w=1.0f/w;
        v->varying.uv[0]=hk_uv[i][0]; v->varying.uv[1]=hk_uv[i][1];
        v->varying.normal[0]=cy*nx+sy*nz; v->varying.normal[1]=ny; v->varying.normal[2]=-sy*nx+cy*nz;
        v->varying.light[0]=0; v->varying.light[1]=0; v->varying.light[2]=1;
        v->varying.half_vector[0]=v->varying.half_vector[1]=v->varying.half_vector[2]=0;
        v->varying.light_clip[0]=rx; v->varying.light_clip[1]=y; v->varying.light_clip[2]=-rz; v->varying.light_clip[3]=1;
        light_verts[i].x=(int)((rx+1)*32.0f); light_verts[i].y=(int)((y+1)*32.0f); light_verts[i].depth=(-rz+1)*0.5f;
    }
    tg_shadow_clear(&map);
#ifndef TG_DISABLE_SHADOW
    for(i=0;i<HK_INDEX_COUNT;i+=3) tg_shadow_triangle(&map,&light_verts[hk_indices[i]],&light_verts[hk_indices[i+1]],&light_verts[hk_indices[i+2]]);
#endif
    previous_frame=frame;
}
void cube_render_band(unsigned int *tile,int *depth,int quadrant,unsigned int frame,int row0,int row1)
{
    tg_context c; unsigned int i; int errors=0;
    if(frame!=previous_frame) prepare(frame);
    c.color=tile; c.depth=depth; c.tile_x=(quadrant&1)*80; c.tile_y=(quadrant>>1)*60;
    c.clip_enabled=1; c.clip_x0=c.tile_x; c.clip_x1=c.tile_x+80; c.clip_y0=c.tile_y+row0; c.clip_y1=c.tile_y+row1;
    tg_clear_band(&c,0xff090e14u,row0,row1);
    for(i=0;i<HK_INDEX_COUNT;i+=3) errors+=tg_material_triangle(&c,&verts[hk_indices[i]],&verts[hk_indices[i+1]],&verts[hk_indices[i+2]],&material);
    if(errors) for(i=(unsigned int)(row0*80);i<(unsigned int)(row1*80);i++)tile[i]=0xffff00ffu;
}
void cube_render(unsigned int *tile,int *depth,int quadrant,unsigned int frame)
{ cube_render_band(tile,depth,quadrant,frame,0,60); }
