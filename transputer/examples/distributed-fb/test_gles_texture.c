#include "gles2_texture.h"
#include "glsl_vm.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static tg_context target; static tg_gles_state gl; static tg_gles_texture_state textures;
static unsigned int storage[128]; static tg_vm_state vm;
static int run(void)
{
    static const unsigned char data[]={255,0,0,255,0,255,0,128};
    static const tg_vm_instruction sample={TG_VM_TEXTURE2D,1,0,0,4,0,0};
    GLuint ids[2]; const tg_fragment_texture *view;
    tgGlesInit(&gl,&target); tgGlesMakeCurrent(&gl); tgGlesInitTextures(&gl,&textures,storage,sizeof(storage));
    glGenTextures(2,ids); if(glGetError()!=GL_NO_ERROR || !ids[0] || ids[0]==ids[1])return 1;
    glBindTexture(GL_TEXTURE_2D,ids[0]); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,(GLfloat)GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,1,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    if(glGetError()!=GL_NO_ERROR || !(view=tgGlesTextureUnit(0)))return 2;
    vm.texture[0]=view; vm.reg[0][0]=1; vm.reg[0][1]=0;
    if(tg_glsl_vm_run(&vm,&sample,1)!=TG_VM_OK || vm.reg[1][1]!=1 || vm.reg[1][3]<0.501f || vm.reg[1][3]>0.502f)return 3;
    glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,(GLfloat)GL_LINEAR);vm.reg[0][0]=0.5f;
    if(glGetError()!=GL_NO_ERROR || tg_glsl_vm_run(&vm,&sample,1)!=TG_VM_OK || vm.reg[1][0]!=0.5f || vm.reg[1][1]!=0.5f)return 8;
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,20,20,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    if(glGetError()!=GL_OUT_OF_MEMORY || tgGlesTextureUnit(0)->width!=2)return 4;
    glActiveTexture(GL_TEXTURE0+1); glBindTexture(GL_TEXTURE_2D,ids[1]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    if(glGetError()!=GL_NO_ERROR || !tgGlesTextureUnit(1) || tgGlesTextureUnit(0)->width!=2)return 5;
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,0xffff);
    if(glGetError()!=GL_INVALID_ENUM)return 6;
    glDeleteTextures(2,ids);
    if(tgGlesTextureUnit(0) || tgGlesTextureUnit(1) || glGetError()!=GL_NO_ERROR)return 7;
    tgGlesMakeCurrent(0); return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void) { volatile unsigned int *u=(volatile unsigned int *)0x10000000u; int n=run(); const char *p=n?"TEXTURE BAD\n":"TEXTURE OK\n"; while(*p)*u=*p++; return n; }
#else
int main(void) { int n=run(); if(!n) puts("GLES texture host tests OK"); return n; }
#endif
