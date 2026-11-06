/* OpenGL API exercise with original uber sources. This is a small diagnostic
 * triangle, not dEngine's renderer or its demo scene. */
#include "../distributed-fb/gles2_program.h"
#include "../distributed-fb/gles2_texture.h"
#ifdef TG_GLES_FRAMEBUFFER
#include "../distributed-fb/gles2_framebuffer.h"
#endif
#include "uber_gl_sources.h"
static tg_context target;static tg_gles_state gl;static tg_gles_draw_state draw;static tg_gles_program_state programs;
static tg_gles_texture_state textures;static unsigned int arena[256];static GLuint program;static int initialized,failed;
#ifdef TG_GLES_FRAMEBUFFER
static tg_gles_framebuffer_state framebuffers;static unsigned int scratch[4];static int rb_arena[16];
#endif
/* Minimal ABI matching the fields consumed by dEngine's unmodified OBJ draw
 * function. The function body is extracted from the local checkout at build
 * time, not copied into this repository. */
typedef struct { float position[3];short normal[3],tangent[3];float textCoo[2]; } obj_vertex_t;
typedef struct { obj_vertex_t *vertices;unsigned short num_vertices;unsigned short *indices,num_indices; } obj_t;
typedef struct { GLuint vars[32]; } shader_prog_t;
static struct { int isRenderingShadow; } renderer;
static shader_prog_t shader_storage;static shader_prog_t *currentShader=&shader_storage;
#define SHADER_ATT_VERTEX 1
#define SHADER_ATT_NORMAL 2
#define SHADER_ATT_UV 3
#define SHADER_ATT_TANGENT 4
static void STATS_AddTriangles(int count){(void)count;}
#include "upstream_obj_draw.h"
static obj_vertex_t vertices[]={{{-0.6f,0.7f,0},{0,0,32767},{32767,0,0},{0,0}},{{-0.6f,-0.7f,0},{0,0,32767},{32767,0,0},{0,1}},{{0.6f,0.7f,0},{0,0,32767},{32767,0,0},{1,0}}};
static unsigned short indices[]={0,1,2};static obj_t object={vertices,3,indices,3};
static const unsigned char base_pixels[]={220,90,40,255, 30,170,210,255, 30,170,210,255, 220,90,40,255};
static const unsigned char bump_pixels[]={128,128,255,255},spec_pixels[]={80,80,80,255},shadow_pixels[]={0,0,0,255};
static const float ambient[]={0.5f,0.5f,0.5f},diffuse[]={0.5f,0.5f,0.5f},specular[]={0.1f,0.1f,0.1f},position[]={0,0,3};
static const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static GLuint compile(GLenum type,const char *source)
{
    static const char *defines[]={"#define BUMP_MAPPING\n#define SPEC_MAPPING\n#define SHADO_MAPPING\n",0};
    const char *strings[2];GLuint id;GLint ok;strings[0]=defines[0];strings[1]=source;
    id=glCreateShader(type);glShaderSource(id,2,strings,0);glCompileShader(id);glGetShaderiv(id,GL_COMPILE_STATUS,&ok);if(!ok)failed=1;return id;
}
static void init(void)
{
    GLuint v,f,ids[4];GLint ok,a;int i;const unsigned char *pixels[]={base_pixels,bump_pixels,spec_pixels,shadow_pixels};
    tgGlesInit(&gl,&target);tgGlesMakeCurrent(&gl);tgGlesInitDraw(&gl,&draw,0,0,0,0);tgGlesInitPrograms(&gl,&programs);tgGlesInitTextures(&gl,&textures,arena,sizeof(arena));
#ifdef TG_GLES_FRAMEBUFFER
    tgGlesInitFramebuffers(&gl,&framebuffers,scratch,4,rb_arena,16);
#endif
    glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);glCullFace(GL_BACK);
    v=compile(GL_VERTEX_SHADER,vertex_uber_source);f=compile(GL_FRAGMENT_SHADER,fragment_uber_source);
    program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok)failed=1;glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program,"lightPOVPVMMatrix"),1,GL_FALSE,identity);
    glUniform3fv(glGetUniformLocation(program,"lightPosition"),1,position);glUniform3fv(glGetUniformLocation(program,"cameraPosition"),1,position);
    glUniform3fv(glGetUniformLocation(program,"lightColorAmbient"),1,ambient);glUniform3fv(glGetUniformLocation(program,"lightColorDiffuse"),1,diffuse);
    glUniform3fv(glGetUniformLocation(program,"lightColorSpecular"),1,specular);glUniform3fv(glGetUniformLocation(program,"matColorSpecular"),1,specular);
    glUniform1f(glGetUniformLocation(program,"materialShininess"),2);
    glUniform1i(glGetUniformLocation(program,"s_baseMap"),0);glUniform1i(glGetUniformLocation(program,"s_bumpMap"),1);
    glUniform1i(glGetUniformLocation(program,"s_specularMap"),2);glUniform1i(glGetUniformLocation(program,"s_shadowpMap"),3);
    a=glGetAttribLocation(program,"a_vertex");currentShader->vars[SHADER_ATT_VERTEX]=a;glEnableVertexAttribArray(a);
    a=glGetAttribLocation(program,"a_normal");currentShader->vars[SHADER_ATT_NORMAL]=a;glEnableVertexAttribArray(a);
    a=glGetAttribLocation(program,"a_tangent");currentShader->vars[SHADER_ATT_TANGENT]=a;glEnableVertexAttribArray(a);
    a=glGetAttribLocation(program,"a_texcoord0");currentShader->vars[SHADER_ATT_UV]=a;glEnableVertexAttribArray(a);
    glGenTextures(4,ids);for(i=0;i<4;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,ids[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,i?1:2,i?1:2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels[i]);}
#ifdef TG_GLES_FRAMEBUFFER
    {
        GLuint output,fb,rb;
        glActiveTexture(GL_TEXTURE0);glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,0);glBindTexture(GL_TEXTURE_2D,ids[0]);
        glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
        glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,2,2);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,rb);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)failed=1;
        glViewport(0,0,2,2);glClearColor(0.1f,0.15f,0.2f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(glGetUniformLocation(program,"modelViewProjectionMatrix"),1,GL_FALSE,identity);RenderEntitiesOBJ(&object);
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,160,120);glBindTexture(GL_TEXTURE_2D,output);
    }
#endif
    if(glGetError()!=GL_NO_ERROR)failed=1;initialized=1;
}
void cube_render_band(unsigned int *tile,int *depth,int quadrant,unsigned int frame,int row0,int row1)
{
    float matrix[16];int i;
    target.color=tile;target.depth=depth;target.tile_x=(quadrant&1)*80;target.tile_y=(quadrant>>1)*60;
    if(!initialized)init();else tgGlesMakeCurrent(&gl);
    glDisable(GL_SCISSOR_TEST);if(row0!=0||row1!=60){glScissor(target.tile_x,120-target.tile_y-row1,80,row1-row0);glEnable(GL_SCISSOR_TEST);}
    glClearColor(0.035f,0.055f,0.08f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    for(i=0;i<16;i++)matrix[i]=identity[i];matrix[12]=(frame&1)?0.08f:0;
    glUniformMatrix4fv(glGetUniformLocation(program,"modelViewProjectionMatrix"),1,GL_FALSE,matrix);
    RenderEntitiesOBJ(&object);if(glGetError()!=GL_NO_ERROR)failed=1;
    if(failed){glDisable(GL_SCISSOR_TEST);glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);}
    tgGlesMakeCurrent(0);
}
void cube_render(unsigned int *tile,int *depth,int quadrant,unsigned int frame){cube_render_band(tile,depth,quadrant,frame,0,60);}
