#include "../distributed-fb/gles2_program.h"
#include "uber_source.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static tg_context target;static tg_gles_state gl;static tg_gles_draw_state draw;static tg_gles_program_state programs;
static const char *defs[]={"#define BUMP_MAPPING\n","#define SPEC_MAPPING\n","#define SHADO_MAPPING\n"};
static const char *undefs[]={"#undef BUMP_MAPPING\n","#undef SPEC_MAPPING\n","#undef SHADO_MAPPING\n"};
static GLuint build(GLenum type,const char *source,int mode)
{
    const char *strings[4];GLuint id;GLint ok;int i,n=0;
    for(i=0;i<3;i++)strings[n++]=(mode&(1<<i))?defs[i]:undefs[i];strings[n++]=source;
    id=glCreateShader(type);glShaderSource(id,n,strings,0);glCompileShader(id);glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
    return ok?id:0;
}
static int run(void)
{
    int mode;GLuint v,f,p;GLint ok;
    tgGlesInit(&gl,&target);tgGlesMakeCurrent(&gl);tgGlesInitDraw(&gl,&draw,0,0,0,0);tgGlesInitPrograms(&gl,&programs);
    for(mode=0;mode<8;mode++) {
        v=build(GL_VERTEX_SHADER,vertex_source,mode);f=build(GL_FRAGMENT_SHADER,fragment_source,mode);
        if(!v||!f)return 1;p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
        glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok)return 2;
        glUseProgram(p);if(glGetError()!=GL_NO_ERROR)return 3;
        glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);
    }
    glUseProgram(0);v=build(GL_VERTEX_SHADER,vertex_source,1);f=build(GL_FRAGMENT_SHADER,fragment_source,0);
    if(!v||!f)return 4;p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
    glGetProgramiv(p,GL_LINK_STATUS,&ok);if(ok)return 5;
    glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);tgGlesMakeCurrent(0);return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void){volatile unsigned int *u=(volatile unsigned int *)0x10000000u;int n=run();const char *p=n?"UBER API BAD\n":"UBER API OK\n";while(*p)*u=*p++;return n;}
#else
int main(void){int n=run();if(!n)puts("uber compile/link host tests OK");return n;}
#endif
