#include "gles2_draw.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static unsigned int tile[80*60];static int depth[80*60],calls,offset;
static tg_context target;static tg_gles_state gl;static tg_gles_draw_state draw;
static const float points[][2]={{10,10},{10,40},{40,10},{40,40}};
static const unsigned char index[]={0,1,2,2,1,3};
static int vertex(const GLfloat a[TG_GLES_MAX_ATTRIBUTES][4],tg_material_vertex *out,void *user)
{ static const tg_fragment_varyings zero={0};(void)user;calls++;out->x=(int)a[0][0]+offset;out->y=(int)a[0][1];out->depth=1;out->inverse_w=1;out->varying=zero;return 1; }
static int fragment(void *user,const tg_fragment_varyings *v,float out[4])
{ (void)user;(void)v;out[0]=1;out[1]=out[2]=0;out[3]=1;return TG_FRAGMENT_OK; }
static int run(void)
{
    target.color=tile;target.depth=depth;tgGlesInit(&gl,&target);tgGlesMakeCurrent(&gl);tgGlesInitDraw(&gl,&draw,0,0,0,0);
    draw.material_vertex=vertex;draw.material_fragment=fragment;glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,points);glEnableVertexAttribArray(0);
    tg_clear(&target,0xff000000u);glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_BYTE,index);
    if(glGetError()!=GL_NO_ERROR||calls!=4||tile[20*80+20]!=0xffff0000u)return 1;
    offset=20;calls=0;tg_clear(&target,0xff000000u);glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_BYTE,index);
    if(glGetError()!=GL_NO_ERROR||calls!=4||tile[20*80+20]!=0xff000000u||tile[20*80+40]!=0xffff0000u)return 2;
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void){volatile unsigned int *u=(volatile unsigned int *)0x10000000u;int n=run();const char *p=n?"VM CACHE BAD\n":"VM CACHE OK\n";while(*p)*u=*p++;return n;}
#else
int main(void){int n=run();if(!n)puts("VM vertex cache host tests OK");return n;}
#endif
