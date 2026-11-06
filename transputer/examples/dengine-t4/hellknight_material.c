/* Pre-skinned idle preview using the material rasterizer and a per-node shadow pass.
 * This is a native C material path, not the original dEngine GLSL renderer. */
#include "../distributed-fb/material_raster.h"
#include "../distributed-fb/shadow_map.h"
#ifdef TG_DEMO_ROOM
#include "demo_room.h"
#include "../distributed-fb/gles2_uber_vertex.h"
#ifdef TG_DEMO_VERTEX_VM
#include "../distributed-fb/gles2_uber_vm.h"
#endif
#ifndef __transputer__
#include <math.h>
#endif
#endif
#include "hellknight_asset.h"
static const int sine[32]={0,50,98,142,181,213,237,251,256,251,237,213,181,142,98,50,0,-50,-98,-142,-181,-213,-237,-251,-256,-251,-237,-213,-181,-142,-98,-50};
static tg_material_vertex verts[HK_VERTEX_COUNT];
static tg_shadow_vertex light_verts[HK_VERTEX_COUNT];
static float map_depth[64*64]; static unsigned char map_pixels[64*64*4];
static tg_shadow_map map={64,64,map_depth,map_pixels};
static tg_fragment_texture base={hk_diffuse,32,32,TG_FRAG_REPEAT,TG_FRAG_REPEAT};
static tg_fragment_texture bump={hk_bump,32,32,TG_FRAG_REPEAT,TG_FRAG_REPEAT};
static tg_fragment_texture specular={hk_specular,32,32,TG_FRAG_REPEAT,TG_FRAG_REPEAT};
static tg_fragment_texture shadow={map_pixels,64,64,TG_FRAG_CLAMP,TG_FRAG_CLAMP};
static tg_fragment_material material;
static unsigned int previous_frame=~0u;
static float square(float x,float exponent) { (void)exponent; return x*x; }
#ifdef TG_DEMO_ROOM
static float shader_root(float x) {
#ifdef __transputer__
    return __fpsqrt(x);
#else
    return sqrtf(x);
#endif
}
#endif
static void prepare(unsigned int frame)
{
    unsigned int i, pose=(frame/15)%HK_ANIMATION_FRAMES, next=(pose+1)%HK_ANIMATION_FRAMES;
    float blend=(float)(frame%15)/15.0f,sy=sine[frame&31]/256.0f,cy=sine[(frame+8)&31]/256.0f;
#ifdef TG_DEMO_ROOM
    float camera_x=-HK_CENTER_X*HK_WORLD_SCALE, camera_y=(80.0f-HK_CENTER_Y)*HK_WORLD_SCALE;
    float camera_z=(100.0f-HK_CENTER_Z)*HK_WORLD_SCALE;
    static tg_uber_vertex_uniforms shader_uniforms;
    sy=0; cy=1;
    if(previous_frame==~0u) demo_room_init(HK_CENTER_X,HK_CENTER_Y,HK_CENTER_Z,HK_WORLD_SCALE,camera_x,camera_y,camera_z);
    shader_uniforms.mvp[0]=0.9f; shader_uniforms.mvp[5]=1.2f;
    shader_uniforms.mvp[11]=-1.0f; shader_uniforms.mvp[12]=-0.9f*camera_x;
    shader_uniforms.mvp[13]=-1.2f*camera_y; shader_uniforms.mvp[15]=camera_z;
    shader_uniforms.light_mvp[0]=shader_uniforms.light_mvp[5]=shader_uniforms.light_mvp[10]=0.25f;
    shader_uniforms.light_mvp[10]=-0.25f; shader_uniforms.light_mvp[15]=1.0f;
    shader_uniforms.light_position[0]=shader_uniforms.light_position[1]=0; shader_uniforms.light_position[2]=4;
    shader_uniforms.camera_position[0]=camera_x; shader_uniforms.camera_position[1]=camera_y; shader_uniforms.camera_position[2]=camera_z;
    shader_uniforms.features=TG_FRAG_BUMP|TG_FRAG_SHADOW; shader_uniforms.sqrt_fn=shader_root;
#endif
#ifdef TG_DISABLE_ROTATION
    sy=0; cy=1;
#endif
    material.base=&base; material.shadow=&shadow; material.bump=&bump; material.specular=&specular;
#ifdef TG_DISABLE_SHADOW
    material.features=TG_FRAG_BUMP|TG_FRAG_SPEC;
#else
    material.features=TG_FRAG_SHADOW|TG_FRAG_BUMP|TG_FRAG_SPEC;
#endif
#ifdef TG_DISABLE_BUMP
    material.features&=~TG_FRAG_BUMP;
#endif
#ifdef TG_DISABLE_SPECULAR
    material.features&=~TG_FRAG_SPEC;
#endif
    material.pow_fn=square; material.shininess=2; material.clamp_specular_base=1;
    for(i=0;i<3;i++) { material.ambient[i]=0.35f; material.diffuse[i]=0.75f; material.specular_light[i]=0.3f; }
    for(i=0;i<HK_VERTEX_COUNT;i++) {
        const short *position=hk_animated[pose][i], *position_next=hk_animated[next][i];
        const short *normal=hk_animated_normals[pose][i], *normal_next=hk_animated_normals[next][i];
        const short *tangent=hk_animated_tangents[pose][i], *tangent_next=hk_animated_tangents[next][i];
        float x,y,z,nx,ny,nz,tx,ty,tz,bz,lx,lz;
#ifdef TG_DISABLE_ANIMATION
        position=position_next=hk_vertices[i]; normal=normal_next=hk_normals[i];
        tangent=tangent_next=hk_tangents[i];
#endif
        x=(position[0]+(position_next[0]-position[0])*blend)/32767.0f;
        y=(position[1]+(position_next[1]-position[1])*blend)/32767.0f;
        z=(position[2]+(position_next[2]-position[2])*blend)/32767.0f;
        float rx=cy*x+sy*z,rz=-sy*x+cy*z;
#ifdef TG_DEMO_ROOM
        float w=camera_z-rz;
#else
        float w=3.0f-rz;
#endif
        nx=(normal[0]+(normal_next[0]-normal[0])*blend)/32767.0f;
        ny=(normal[1]+(normal_next[1]-normal[1])*blend)/32767.0f;
        nz=(normal[2]+(normal_next[2]-normal[2])*blend)/32767.0f;
        tx=(tangent[0]+(tangent_next[0]-tangent[0])*blend)/32767.0f;
        ty=(tangent[1]+(tangent_next[1]-tangent[1])*blend)/32767.0f;
        tz=(tangent[2]+(tangent_next[2]-tangent[2])*blend)/32767.0f;
        tg_material_vertex *v=&verts[i];
#ifndef TG_DEMO_ROOM
        v->x=(int)(80.0f+110.0f*rx/w); v->y=(int)(60.0f-110.0f*y/w);
        v->depth=(int)(100000.0f/w); v->inverse_w=1.0f/w;
#endif
        v->varying.uv[0]=hk_uv[i][0]; v->varying.uv[1]=hk_uv[i][1];
        v->varying.normal[0]=cy*nx+sy*nz; v->varying.normal[1]=ny; v->varying.normal[2]=-sy*nx+cy*nz;
#ifdef TG_DEMO_ROOM
        {
            tg_uber_vertex_input input; tg_uber_vertex_output output;
            input.position[0]=rx; input.position[1]=y; input.position[2]=rz;
            input.normal[0]=v->varying.normal[0]; input.normal[1]=ny; input.normal[2]=v->varying.normal[2];
            input.tangent[0]=cy*tx+sy*tz; input.tangent[1]=ty; input.tangent[2]=-sy*tx+cy*tz;
            input.uv[0]=hk_uv[i][0]; input.uv[1]=hk_uv[i][1];
#ifdef TG_DEMO_VERTEX_VM
            if(tg_uber_vertex_vm(&input,&shader_uniforms,&output)==TG_FRAGMENT_OK) {
#else
            if(tg_uber_vertex(&input,&shader_uniforms,&output)==TG_FRAGMENT_OK) {
#endif
                v->varying=output.varying;
                v->x=(int)(80.0f+80.0f*output.clip[0]/output.clip[3]);
                v->y=(int)(60.0f-60.0f*output.clip[1]/output.clip[3]);
                v->depth=(int)(100000.0f/output.clip[3]); v->inverse_w=1.0f/output.clip[3];
            } else {
                /* A zero-length shader normalization is undefined. Keep a
                 * deterministic ambient-only fallback for that vertex. */
                v->x=(int)(80.0f+72.0f*(rx-camera_x)/w); v->y=(int)(60.0f-72.0f*(y-camera_y)/w);
                v->depth=(int)(100000.0f/w); v->inverse_w=1.0f/w;
                v->varying.light[0]=v->varying.light[1]=v->varying.light[2]=0;
                v->varying.half_vector[0]=v->varying.half_vector[1]=v->varying.half_vector[2]=0;
            }
        }
#else
        /* World-space light and eye are both approximated by +Z. Transform
         * the direction into the model tangent frame, as v_uber does. */
        lx=-sy; lz=cy; bz=nx*ty-ny*tx;
        v->varying.light[0]=lx*tx+lz*tz;
        v->varying.light[1]=lx*(ny*tz-nz*ty)+lz*bz;
        v->varying.light[2]=lx*nx+lz*nz;
        v->varying.half_vector[0]=v->varying.light[0];
        v->varying.half_vector[1]=v->varying.light[1];
        v->varying.half_vector[2]=v->varying.light[2];
#ifdef TG_DISABLE_BUMP
        v->varying.light[0]=v->varying.half_vector[0]=0;
        v->varying.light[1]=v->varying.half_vector[1]=0;
        v->varying.light[2]=v->varying.half_vector[2]=1;
#endif
#endif
#ifdef TG_DEMO_ROOM
        v->varying.light_clip[0]=rx*0.25f; v->varying.light_clip[1]=y*0.25f; v->varying.light_clip[2]=-rz*0.25f;
        light_verts[i].x=(int)((rx*0.25f+1)*32.0f); light_verts[i].y=(int)((y*0.25f+1)*32.0f); light_verts[i].depth=(-rz*0.25f+1)*0.5f;
#else
        v->varying.light_clip[0]=rx; v->varying.light_clip[1]=y; v->varying.light_clip[2]=-rz;
        light_verts[i].x=(int)((rx+1)*32.0f); light_verts[i].y=(int)((y+1)*32.0f); light_verts[i].depth=(-rz+1)*0.5f;
#endif
        v->varying.light_clip[3]=1;
    }
    tg_shadow_clear(&map);
#ifndef TG_DISABLE_SHADOW
    for(i=0;i<HK_INDEX_COUNT;i+=3) tg_shadow_triangle(&map,&light_verts[hk_indices[i]],&light_verts[hk_indices[i+1]],&light_verts[hk_indices[i+2]]);
#if defined(TG_DEMO_ROOM) && !defined(TG_DISABLE_ROOM_SHADOW)
    demo_room_shadow(&map);
#endif
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
#if defined(TG_DEMO_ROOM) && !defined(TG_DISABLE_ROOM_DRAW)
    errors+=demo_room_draw(&c,&shadow,0);
#endif
    for(i=0;i<HK_INDEX_COUNT;i+=3) errors+=tg_material_triangle(&c,&verts[hk_indices[i]],&verts[hk_indices[i+1]],&verts[hk_indices[i+2]],&material);
#if defined(TG_DEMO_ROOM) && !defined(TG_DISABLE_ROOM_DRAW)
    errors+=demo_room_draw(&c,&shadow,1);
#endif
    if(errors) for(i=(unsigned int)(row0*80);i<(unsigned int)(row1*80);i++)tile[i]=0xffff00ffu;
}
void cube_render(unsigned int *tile,int *depth,int quadrant,unsigned int frame)
{ cube_render_band(tile,depth,quadrant,frame,0,60); }
