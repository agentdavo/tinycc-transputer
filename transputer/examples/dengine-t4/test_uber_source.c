#include "../distributed-fb/gles2_program.h"
#include "uber_source.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static tg_context context; static tg_gles_state gl; static tg_gles_program_state programs;
static int same(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return *a==*b; }
static int run(void)
{
    static const char *flags[]={"#define BUMP_MAPPING\n", "#define SPEC_MAPPING\n", "#define SHADO_MAPPING\n"};
    const char *src[4]; char log[128]; GLuint id; GLint ok; int mode,stage,n,i;
    tgGlesInit(&gl,&context); tgGlesMakeCurrent(&gl); tgGlesInitPrograms(&gl,&programs);
    for(stage=0;stage<2;stage++) for(mode=0;mode<8;mode++) {
        n=0; for(i=0;i<3;i++)if(mode&(1<<i))src[n++]=flags[i];
        src[n++]=stage?fragment_source:vertex_source;
        id=glCreateShader(stage?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);
        glShaderSource(id,n,src,0); glCompileShader(id); glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
        glGetShaderInfoLog(id,sizeof(log),0,log);
        /* The real source reaches the parser but must not be reported as
         * compiled while its arithmetic and varyings are unimplemented. */
        if(ok || !same(log,"unsupported GLSL ES shader syntax or operations")) return 1;
        glDeleteShader(id);
    }
    tgGlesMakeCurrent(0); return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void) { volatile unsigned int *u=(volatile unsigned int *)0x10000000u; int n=run(); const char *p=n?"UBER BAD\n":"UBER OK\n"; while(*p)*u=*p++; return n; }
#else
int main(void) { int n=run(); if(!n) puts("dEngine uber preprocessing host tests OK"); return n; }
#endif
