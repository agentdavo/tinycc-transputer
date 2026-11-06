/* Hellknight bind-pose geometry preview. Generated model data stays in the
 * run directory. Flat face lighting is baked; no original material, skeletal
 * animation, GLSL uber shader, or shadow pass is claimed. */
#include "../distributed-fb/gles2_program.h"
#include "../dEngine/src/matrix.h"
#include "hellknight_asset.h"

static const char *vs="attribute vec3 p; uniform mat4 m; void main(){ gl_Position=m*vec4(p,1.0); }";
static const char *fs="precision mediump float; uniform vec4 c; void main(){ gl_FragColor=c; }";
static const int sine[32]={0,50,98,142,181,213,237,251,256,251,237,213,181,142,98,50,0,-50,-98,-142,-181,-213,-237,-251,-256,-251,-237,-213,-181,-142,-98,-50};
static tg_context context; static tg_gles_state gl; static tg_gles_draw_state draw_state;
static tg_gles_program_state programs; static GLuint program_id; static int initialized, failed;
#ifdef __transputer__
#define HK_ARENA ((void *)0x80100000u)
#else
static unsigned int hk_host_arena[32768];
#define HK_ARENA hk_host_arena
#endif
#define HK_ARENA_BYTES (128u*1024u)

static void init(void)
{
    GLuint v,f,buffers[2]; GLint status;
    tgGlesInit(&gl,&context); tgGlesMakeCurrent(&gl);
    tgGlesInitDraw(&gl,&draw_state,HK_ARENA,HK_ARENA_BYTES,0,0);
    tgGlesInitPrograms(&gl,&programs);
    v=glCreateShader(GL_VERTEX_SHADER); f=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v,1,&vs,0); glCompileShader(v); glGetShaderiv(v,GL_COMPILE_STATUS,&status); if(!status) failed=1;
    glShaderSource(f,1,&fs,0); glCompileShader(f); glGetShaderiv(f,GL_COMPILE_STATUS,&status); if(!status) failed=1;
    program_id=glCreateProgram(); glAttachShader(program_id,v); glAttachShader(program_id,f);
    glBindAttribLocation(program_id,0,"p"); glLinkProgram(program_id); glGetProgramiv(program_id,GL_LINK_STATUS,&status); if(!status) failed=1;
    glUseProgram(program_id);
    glGenBuffers(2,buffers);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[0]); glBufferData(GL_ARRAY_BUFFER,sizeof(hk_vertices),hk_vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_SHORT,GL_TRUE,0,0); glEnableVertexAttribArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(hk_indices),hk_indices,GL_STATIC_DRAW);
    if(glGetError()!=GL_NO_ERROR) failed=1;
    initialized=1;
}

void cube_render_band(unsigned int *tile, int *depth, int quadrant, unsigned int frame, int row0, int row1)
{
    matrix_t rotation,view,projection,mv,mvp; int i,group; unsigned int first=0;
    float sy=sine[frame&31]/256.0f, cy=sine[(frame+8)&31]/256.0f;
    context.color=tile; context.depth=depth; context.tile_x=(quadrant&1)*80; context.tile_y=(quadrant>>1)*60;
    if(!initialized) init(); else tgGlesMakeCurrent(&gl);
    glDisable(GL_SCISSOR_TEST);
    if(row0!=0 || row1!=60) { glScissor(context.tile_x,120-context.tile_y-row1,80,row1-row0); glEnable(GL_SCISSOR_TEST); }
    glClearColor(0.035f,0.055f,0.08f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    matrixLoadIdentity(rotation); matrixLoadIdentity(view);
    rotation[0]=cy; rotation[2]=-sy; rotation[8]=sy; rotation[10]=cy; view[14]=-3.0f;
    matrix_multiply(view,rotation,mv);
    for(i=0;i<16;i++) projection[i]=0.0f;
    projection[0]=1.375f; projection[5]=1.8333333f; projection[10]=-1.0f; projection[11]=-1.0f; projection[14]=-0.2f;
    matrix_multiply(projection,mv,mvp);
    glUniformMatrix4fv(glGetUniformLocation(program_id,"m"),1,GL_FALSE,mvp);
    for(group=0;group<16;group++) {
        float light=0.22f+(float)group*(0.78f/15.0f);
        glUniform4f(glGetUniformLocation(program_id,"c"),light*0.68f,light*0.48f,light*0.34f,1.0f);
        glDrawElements(GL_TRIANGLES,hk_group_counts[group],GL_UNSIGNED_SHORT,(const void *)(unsigned long)(first*2u));
        first+=hk_group_counts[group];
    }
    if(glGetError()!=GL_NO_ERROR) failed=1;
    if(failed) { glDisable(GL_SCISSOR_TEST); glClearColor(1,0,1,1); glClear(GL_COLOR_BUFFER_BIT); }
    tgGlesMakeCurrent(0);
}
void cube_render(unsigned int *tile, int *depth, int quadrant, unsigned int frame)
{
    cube_render_band(tile,depth,quadrant,frame,0,60);
}
