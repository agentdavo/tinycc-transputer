#include "gles2_framebuffer.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static tg_context target;static tg_gles_state gl;static tg_gles_texture_state textures;static tg_gles_framebuffer_state fbs;
static unsigned int screen[80*60],texture_arena[8*12],scratch[12];static int screen_depth[80*60],depth_arena[4*12];
static int run(void)
{
    GLuint texture,fb,rb;unsigned char pixels[4*12];const tg_fragment_texture *view;
    target.color=screen;target.depth=screen_depth;tgGlesInit(&gl,&target);tgGlesMakeCurrent(&gl);
    tgGlesInitTextures(&gl,&textures,texture_arena,sizeof(texture_arena));tgGlesInitFramebuffers(&gl,&fbs,scratch,12,depth_arena,48);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,3,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
    glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)return 1;
    glClear(GL_COLOR_BUFFER_BIT);if(glGetError()!=GL_INVALID_FRAMEBUFFER_OPERATION)return 2;
    glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,4,3);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,rb);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||glGetError()!=GL_NO_ERROR)return 3;
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glScissor(0,0,2,1);glEnable(GL_SCISSOR_TEST);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    glReadPixels(0,0,4,3,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    if(glGetError()!=GL_NO_ERROR||pixels[0]!=0||pixels[1]!=255||pixels[2]!=0||pixels[3]!=255||pixels[8]!=255||pixels[9]!=0||pixels[32]!=255)return 4;
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,3,0,GL_RGBA,GL_UNSIGNED_BYTE,0);if(glGetError()!=GL_INVALID_OPERATION)return 5;
    glBindFramebuffer(GL_FRAMEBUFFER,0);view=tgGlesTextureUnit(0);
    if(!view||view->rgba[1]!=255||view->rgba[8]!=255||view->rgba[32]!=255)return 6;
    glBindFramebuffer(GL_FRAMEBUFFER,fb);glDeleteTextures(1,&texture);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)return 7;
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteRenderbuffers(1,&rb);glDeleteFramebuffers(1,&fb);
    if(glGetError()!=GL_NO_ERROR||gl.target!=&target)return 8;
    return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void){volatile unsigned int *u=(volatile unsigned int *)0x10000000u;int n=run();const char *p=n?"FRAMEBUFFER BAD ":"FRAMEBUFFER OK\n";while(*p)*u=*p++;if(n){*u='0'+n;*u='\n';}return n;}
#else
int main(void){int n=run();if(!n)puts("framebuffer host tests OK");else printf("framebuffer test error %d\n",n);return n;}
#endif
