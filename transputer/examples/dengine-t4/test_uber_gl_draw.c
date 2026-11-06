#include "../distributed-fb/gles2_program.h"
#include "../distributed-fb/gles2_texture.h"
#ifdef TG_GLES_FRAMEBUFFER
#include "../distributed-fb/gles2_framebuffer.h"
#endif
#include "uber_source.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static tg_context target;static tg_gles_state gl;static tg_gles_draw_state draw;static tg_gles_program_state programs;
static tg_gles_texture_state textures;static unsigned int tile[80*60],arena[8*32*24];static int depth[80*60];
#ifdef TG_GLES_FRAMEBUFFER
static tg_gles_framebuffer_state framebuffers;static unsigned int scratch[32*24];static int rb_arena[4*32*24];
#endif
static const float pos[][3]={{-0.5f,0.5f,0},{-0.5f,-0.5f,0},{0.5f,0.5f,0}};
static const float norm[][3]={{0,0,1},{0,0,1},{0,0,1}},uv[][2]={{0,0},{0,0},{0,0}};
static const float tangent[][3]={{1,0,0},{1,0,0},{1,0,0}};
static const unsigned char indices[]={0,1,2};
static const float matrix[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static const float ambient[]={1,1,1},zero[]={0,0,0},position[]={0,0,2};
static const unsigned char red[]={255,0,0,255},blue[]={0,0,255,255};
static const unsigned char bump[]={128,128,255,255},white[]={0,0,0,255};
static const char *defs[]={"#define BUMP_MAPPING\n","#define SPEC_MAPPING\n","#define SHADO_MAPPING\n"};
static GLuint compile(GLenum type,const char *source,int mode)
{ const char *strings[4];int i,n=0;GLuint s=glCreateShader(type);GLint ok;for(i=0;i<3;i++)if(mode&(1<<i))strings[n++]=defs[i];strings[n++]=source;glShaderSource(s,n,strings,0);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&ok);return ok?s:0; }
static int run(void)
{
    GLuint v,f,p,tex[4];GLint a;int mode;target.color=tile;target.depth=depth;
    tgGlesInit(&gl,&target);tgGlesMakeCurrent(&gl);tgGlesInitDraw(&gl,&draw,0,0,0,0);tgGlesInitPrograms(&gl,&programs);tgGlesInitTextures(&gl,&textures,arena,sizeof(arena));
#ifdef TG_GLES_FRAMEBUFFER
    tgGlesInitFramebuffers(&gl,&framebuffers,scratch,32*24,rb_arena,4*32*24);
#endif
    glViewport(0,0,160,120);glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);glCullFace(GL_BACK);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    if(!glIsEnabled(GL_DEPTH_TEST)||!glIsEnabled(GL_CULL_FACE)||glIsEnabled(GL_BLEND))return 8;
    glGenTextures(4,tex);
    glActiveTexture(GL_TEXTURE0+0);glBindTexture(GL_TEXTURE_2D,tex[0]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,red);
    glActiveTexture(GL_TEXTURE0+1);glBindTexture(GL_TEXTURE_2D,tex[1]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,bump);
    glActiveTexture(GL_TEXTURE0+2);glBindTexture(GL_TEXTURE_2D,tex[2]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,blue);
    glActiveTexture(GL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,tex[3]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);
    for(mode=0;mode<8;mode++) {
    v=compile(GL_VERTEX_SHADER,vertex_source,mode);f=compile(GL_FRAGMENT_SHADER,fragment_source,mode);if(!v||!f)return 1;
    p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&a);if(!a)return 2;glUseProgram(p);
    glUniformMatrix4fv(glGetUniformLocation(p,"modelViewProjectionMatrix"),1,GL_FALSE,matrix);
    glUniformMatrix4fv(glGetUniformLocation(p,"lightPOVPVMMatrix"),1,GL_FALSE,matrix);
    glUniform3fv(glGetUniformLocation(p,"lightPosition"),1,position);glUniform3fv(glGetUniformLocation(p,"cameraPosition"),1,position);
    glUniform3fv(glGetUniformLocation(p,"lightColorAmbient"),1,ambient);glUniform3fv(glGetUniformLocation(p,"lightColorDiffuse"),1,zero);
    glUniform3fv(glGetUniformLocation(p,"lightColorSpecular"),1,zero);glUniform3fv(glGetUniformLocation(p,"matColorSpecular"),1,zero);
    glUniform1f(glGetUniformLocation(p,"materialShininess"),2);glUniform1i(glGetUniformLocation(p,"s_baseMap"),0);
    glUniform1i(glGetUniformLocation(p,"s_bumpMap"),1);glUniform1i(glGetUniformLocation(p,"s_specularMap"),2);glUniform1i(glGetUniformLocation(p,"s_shadowpMap"),3);
    a=glGetAttribLocation(p,"a_vertex");if(a<0)return 3;glVertexAttribPointer(a,3,GL_FLOAT,GL_FALSE,0,pos);glEnableVertexAttribArray(a);
    a=glGetAttribLocation(p,"a_normal");if(a<0)return 4;glVertexAttribPointer(a,3,GL_FLOAT,GL_FALSE,0,norm);glEnableVertexAttribArray(a);
    a=glGetAttribLocation(p,"a_texcoord0");if(a<0)return 5;glVertexAttribPointer(a,2,GL_FLOAT,GL_FALSE,0,uv);glEnableVertexAttribArray(a);
    a=glGetAttribLocation(p,"a_tangent");if(a>=0){glVertexAttribPointer(a,3,GL_FLOAT,GL_FALSE,0,tangent);glEnableVertexAttribArray(a);}
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,tex[0]);tg_clear(&target,0xff000000u);glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_BYTE,indices);
    if(glGetError()!=GL_NO_ERROR||tile[40*80+50]!=0xffff0000u)return 6;
    glBindTexture(GL_TEXTURE_2D,tex[2]);tg_clear(&target,0xff000000u);glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_BYTE,indices);
    if(glGetError()!=GL_NO_ERROR||tile[40*80+50]!=0xff0000ffu)return 7;
#ifdef TG_GLES_FRAMEBUFFER
    if(mode==0) {
        GLuint ft,fb,rb;unsigned char pixel[4];const tg_fragment_texture *view;
        glBindTexture(GL_TEXTURE_2D,tex[0]);glActiveTexture(GL_TEXTURE0+3);glGenTextures(1,&ft);glBindTexture(GL_TEXTURE_2D,ft);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,32,24,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
        glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,ft,0);
        glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,32,24);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,rb);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 9;
        glViewport(0,0,32,24);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_BYTE,indices);glReadPixels(10,15,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        if(glGetError()!=GL_NO_ERROR||pixel[0]!=255||pixel[1]||pixel[2])return 10;
        glBindFramebuffer(GL_FRAMEBUFFER,0);view=tgGlesTextureUnit(3);
        if(!view||view->rgba[(15*32+10)*4]!=255)return 11;
        glDeleteFramebuffers(1,&fb);glDeleteRenderbuffers(1,&rb);glDeleteTextures(1,&ft);glViewport(0,0,160,120);
        glActiveTexture(GL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,tex[3]);
    }
#endif
    glUseProgram(0);glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);
    }
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void){volatile unsigned int *u=(volatile unsigned int *)0x10000000u;int n=run();const char *p=n?"UBER DRAW BAD ":"UBER DRAW OK\n";while(*p)*u=*p++;if(n){*u='0'+n;*u='\n';}return n;}
#else
int main(void){int n=run();if(!n)puts("uber GL draw host tests OK");else printf("uber GL draw error %d\n",n);return n;}
#endif
