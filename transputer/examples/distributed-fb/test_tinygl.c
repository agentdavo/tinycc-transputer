/* Host regression checks for depth, winding, textures, clipping and animation. */
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
#include "tinygl.h"
#include "gles2_subset.h"
static unsigned int tile[TG_TILE_W*TG_TILE_H], texture[64];
static int depth[TG_TILE_W*TG_TILE_H];
static int run_tests(void)
{
    tg_context c = {0};
    tg_screen_vertex v[3] = {{10,10,256,128,128},{10,40,256,128,128},{40,10,256,128,128}};
    const unsigned char front[3]={0,1,2}, back[3]={0,2,1};
    unsigned int hashes[4], n; int i, node;
    c.color=tile; c.depth=depth; c.texture=texture; c.tile_x=0; c.tile_y=0;
    {
        tg_gles_state s;
        tgGlesInit(&s,&c); tgGlesMakeCurrent(&s);
        glClearColor(-1.0f,0.5f,2.0f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        if(tile[0]!=0xff0080ffu || depth[0]!=-2147483647 || glGetError()!=GL_NO_ERROR) return 8;
        glColorMask(GL_TRUE,GL_FALSE,GL_FALSE,GL_FALSE);
        glDepthMask(GL_FALSE); depth[0]=17;
        glClearColor(1.0f,1.0f,1.0f,0.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
        if(tile[0]!=0xffff80ffu || depth[0]!=17) return 9;
        glClear(0x80000000u);
        if(glGetError()!=GL_INVALID_VALUE || glGetError()!=GL_NO_ERROR || tile[0]!=0xffff80ffu) return 10;
        tgGlesMakeCurrent(0);
    }
    {
        tg_vertex points[2]={{256,0,256,255,0},{256,0,-256,255,0}};
        tg_screen_vertex projected[2];
        c.matrix[0]=c.matrix[4]=c.matrix[8]=256;
        c.camera_distance=1024; c.focal_length=120;
        tg_transform(&c,points,projected,2);
        if(projected[0].x!=120 || projected[1].x!=104 || projected[0].z<=projected[1].z) return 7;
    }
    for(i=0;i<64;i++) texture[i]=0xff000000u;
    texture[4*8+4]=0xff123456u;
    tg_clear(&c,0xff000000u);
    tg_draw_elements(&c,v,front,1);
    if(tile[20*TG_TILE_W+20]!=0xff123456u || tile[50*TG_TILE_W+50]!=0xff000000u) return 1;
    for(i=0;i<64;i++) texture[i]=0xffabcdefu;
    for(i=0;i<3;i++) v[i].z=128;
    tg_draw_elements(&c,v,front,1);
    if(tile[20*TG_TILE_W+20]!=0xff123456u) return 2;
    for(i=0;i<3;i++) v[i].z=512;
    tg_draw_elements(&c,v,back,1);
    if(tile[20*TG_TILE_W+20]!=0xff123456u) return 3;
    tg_draw_elements(&c,v,front,1);
    if(tile[20*TG_TILE_W+20]!=0xffabcdefu) return 4;
    c.tile_x=80; c.tile_y=60;
    tg_clear(&c,0xff000000u); tg_draw_elements(&c,v,front,1);
    for(i=0;i<TG_TILE_W*TG_TILE_H;i++) if(tile[i]!=0xff000000u) return 5;
    for(node=0;node<4;node++) {
        cube_render(tile,depth,node,0); hashes[node]=tg_checksum(tile,TG_TILE_W*TG_TILE_H);
        cube_render(tile,depth,node,1); n=tg_checksum(tile,TG_TILE_W*TG_TILE_H);
        if(n==hashes[node]) return 6;
    }
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void)
{
    volatile unsigned int *uart=(volatile unsigned int *)0x10000000u;
    const char *msg=run_tests() ? "TINYGL BAD\n" : "TINYGL OK\n";
    while (*msg) *uart=(unsigned char)*msg++;
    return 0;
}
#else
int main(void)
{
    int result=run_tests();
    if (!result) puts("tinygl host tests OK");
    return result;
}
#endif
