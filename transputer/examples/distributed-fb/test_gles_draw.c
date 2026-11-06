#include <stddef.h>
#include "gles2_draw.h"
#ifdef __transputer__
typedef signed short int16_t;
typedef unsigned short uint16_t;
#else
#include <stdint.h>
#endif
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
typedef struct { float xyz[3]; int16_t normal[3]; uint16_t uv[2]; } vertex_data;
static vertex_data vertices[4]={
    {{10,10,256},{-32768,0,32767},{32768,32768}},
    {{10,40,256},{-32768,0,32767},{32768,32768}},
    {{40,10,256},{-32768,0,32767},{32768,32768}},
    {{40,40,256},{-32768,0,32767},{32768,32768}}
};
static const uint16_t indices[6]={0,1,2,2,1,3};
static unsigned int tile[TG_TILE_W*TG_TILE_H], tex[64], arena[2048];
static int depth[TG_TILE_W*TG_TILE_H], calls, invalid;
static int shade(const GLfloat a[TG_GLES_MAX_ATTRIBUTES][4], tg_screen_vertex *out, void *user)
{
    (void)user; calls++;
    if (a[1][0]!=-1.0f || a[1][1]!=0.0f || a[1][2]!=1.0f || a[1][3]!=1.0f) invalid++;
    if (a[2][0]<0.499f || a[2][0]>0.501f || a[3][3]!=1.0f) invalid++;
    out->x=(int)a[0][0]; out->y=(int)a[0][1]; out->z=(int)a[0][2];
    out->u=(int)(a[2][0]*256.0f); out->v=(int)(a[2][1]*256.0f);
    return 1;
}
static void setup(const void *base)
{
    const unsigned char *p=base;
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(vertex_data),p+offsetof(vertex_data,xyz));
    glVertexAttribPointer(1,3,GL_SHORT,GL_TRUE,sizeof(vertex_data),p+offsetof(vertex_data,normal));
    glVertexAttribPointer(2,2,GL_UNSIGNED_SHORT,GL_TRUE,sizeof(vertex_data),p+offsetof(vertex_data,uv));
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
}
static int run(void)
{
    static tg_context c; static tg_gles_state gl; static tg_gles_draw_state ds;
    GLuint names[2]; unsigned int client_hash, i; uint16_t patch=3;
    c.color=tile; c.depth=depth; c.texture=tex; c.tile_x=0; c.tile_y=0;
    for(i=0;i<64;i++) tex[i]=0xff123456u;
    tgGlesInit(&gl,&c); tgGlesMakeCurrent(&gl);
    tgGlesInitDraw(&gl,&ds,arena,sizeof(arena),shade,0);
    tg_clear(&c,0xff000000u); setup(vertices); calls=invalid=0;
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,indices);
    if(glGetError()!=GL_NO_ERROR || calls!=4 || invalid || tile[20*80+20]!=0xff123456u) return 1;
    client_hash=tg_checksum(tile,80*60);
    glGenBuffers(2,names); if(glGetError()!=GL_NO_ERROR || !names[0] || names[0]==names[1]) return 2;
    glBindBuffer(GL_ARRAY_BUFFER,names[0]);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    setup((const void *)0);
    glBindBuffer(GL_ARRAY_BUFFER,0); /* pointer captured the buffer */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,names[1]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,10,2,&patch);
    tg_clear(&c,0xff000000u); calls=invalid=0;
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,(const void *)0);
    if(glGetError()!=GL_NO_ERROR || calls!=4 || invalid || tg_checksum(tile,80*60)!=client_hash) return 3;
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,11,2,&patch);
    if(glGetError()!=GL_INVALID_VALUE) return 4;
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(arena),indices,GL_STATIC_DRAW);
    if(glGetError()!=GL_OUT_OF_MEMORY) return 5;
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0);
    if(glGetError()!=GL_NO_ERROR) return 6;
    glVertexAttribPointer(8,3,GL_FLOAT,0,0,0);
    if(glGetError()!=GL_INVALID_VALUE) return 7;
    glVertexAttribPointer(0,3,0xffff,0,0,0);
    if(glGetError()!=GL_INVALID_ENUM) return 8;
    glDrawElements(GL_TRIANGLES,7,GL_UNSIGNED_SHORT,0);
    if(glGetError()!=GL_INVALID_OPERATION) return 9;
    glDeleteBuffers(2,names);
    tgGlesMakeCurrent(0);
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void)
{
    volatile unsigned int *uart=(volatile unsigned int *)0x10000000u; int n=run();
    const char *p=n?"GLES DRAW BAD ":"GLES DRAW OK\n";
    while(*p) *uart=(unsigned char)*p++;
    if(n) { *uart='0'+n; *uart='\n'; }
    return 0;
}
#else
int main(void) { int n=run(); if(!n) puts("GLES draw host tests OK"); return n; }
#endif
