#include "gles2_program.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static unsigned int tile[80*60]; static int depth[80*60];
static tg_context context; static tg_gles_state gl; static tg_gles_draw_state draw_state;
static tg_gles_program_state programs;
static const GLfloat vertices[3][3]={{-0.5f,0.5f,0.0f},{-0.5f,-0.5f,0.0f},{0.5f,0.5f,0.0f}};
static const unsigned char indices[3]={0,1,2};
static const GLfloat identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
static int run(void)
{
    const char *vs[2]={"// vertex\nattribute vec3 p; uniform mat4 m; ","void main(void){gl_Position=m*vec4(p,1.0);}"};
    const char *fs="precision mediump float; uniform vec4 c; void main(){gl_FragColor=c;}";
    const char *unsupported="attribute vec3 p; void main(){gl_Position=vec4(p,1.0);}";
    GLuint v,f,bad,prog; GLint ok,loc; char log[80]; GLsizei n;
    context.color=tile; context.depth=depth; context.tile_x=context.tile_y=0;
    tgGlesInit(&gl,&context); tgGlesMakeCurrent(&gl);
    tgGlesInitDraw(&gl,&draw_state,0,0,0,0); tgGlesInitPrograms(&gl,&programs);
    v=glCreateShader(GL_VERTEX_SHADER); f=glCreateShader(GL_FRAGMENT_SHADER);
    bad=glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(bad,1,&unsupported,0); glCompileShader(bad); glGetShaderiv(bad,GL_COMPILE_STATUS,&ok);
    if(ok) return 1;
    glGetShaderInfoLog(bad,sizeof(log),&n,log); if(n<=0 || !log[0]) return 2;
    glShaderSource(v,2,vs,0); glCompileShader(v); glGetShaderiv(v,GL_COMPILE_STATUS,&ok); if(!ok) return 3;
    glShaderSource(f,1,&fs,0); glCompileShader(f); glGetShaderiv(f,GL_COMPILE_STATUS,&ok); if(!ok) return 4;
    prog=glCreateProgram(); glAttachShader(prog,v); glAttachShader(prog,f);
    glBindAttribLocation(prog,2,"p"); glLinkProgram(prog); glGetProgramiv(prog,GL_LINK_STATUS,&ok); if(!ok) return 5;
    glDeleteShader(v); glDeleteShader(f);
    glGetShaderiv(v,GL_DELETE_STATUS,&ok); if(!ok) return 11;
    if(glGetAttribLocation(prog,"p")!=2 || glGetAttribLocation(prog,"missing")!=-1) return 6;
    loc=glGetUniformLocation(prog,"m"); if(loc<0 || glGetUniformLocation(prog,"c")<0) return 7;
    glUseProgram(prog); glUniformMatrix4fv(loc,1,GL_FALSE,identity);
    glUniform4f(glGetUniformLocation(prog,"c"),1.0f,0.0f,0.0f,1.0f);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,0,vertices); glEnableVertexAttribArray(2);
    tg_clear(&context,0xff000000u); glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_BYTE,indices);
    if(glGetError()!=GL_NO_ERROR || tile[40*80+50]!=0xffff0000u || tile[10*80+10]!=0xff000000u) return 8;
    glUniformMatrix4fv(loc,1,GL_TRUE,identity); if(glGetError()!=GL_INVALID_VALUE) return 9;
    glDeleteProgram(prog); glGetProgramiv(prog,GL_DELETE_STATUS,&ok); if(!ok) return 12;
    glUseProgram(0); glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_BYTE,indices);
    if(glGetError()!=GL_INVALID_OPERATION) return 10;
    glGetProgramiv(prog,GL_LINK_STATUS,&ok); if(glGetError()!=GL_INVALID_VALUE) return 13;
    tgGlesMakeCurrent(0); return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void)
{
    volatile unsigned int *uart=(volatile unsigned int *)0x10000000u; int n=run();
    const char *p=n ? "GLES PROGRAM BAD " : "GLES PROGRAM OK\n";
    while(*p) *uart=(unsigned char)*p++;
    if(n) { *uart="0123456789ABCDEF"[n]; *uart='\n'; }
    return 0;
}
#else
int main(void) { int n=run(); if(!n) puts("GLES program host tests OK"); return n; }
#endif
