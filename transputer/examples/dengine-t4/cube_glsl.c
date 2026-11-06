/* Array demo using real dEngine matrix.c plus the experimental GLSL program,
 * VBO and glDrawElements path. This is not the full dEngine renderer. */
#include "../distributed-fb/gles2_program.h"
#include "../dEngine/src/matrix.h"

static const GLfloat positions[8][3]={
 {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},
 {1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1}
};
static const unsigned short faces[6][6]={
 {0,1,2,0,2,3},{4,5,6,4,6,7},{1,4,7,1,7,2},
 {5,0,3,5,3,6},{3,2,7,3,7,6},{5,4,1,5,1,0}
};
static const GLfloat colors[6][3]={
 {0.89f,0.30f,0.19f},{0.30f,0.70f,0.85f},{0.9f,0.7f,0.2f},
 {0.6f,0.4f,0.8f},{0.3f,0.75f,0.5f},{0.8f,0.4f,0.6f}
};
static const int sine[32]={0,50,98,142,181,213,237,251,256,251,237,213,181,142,98,50,0,-50,-98,-142,-181,-213,-237,-251,-256,-251,-237,-213,-181,-142,-98,-50};
static const char *vertex_source="attribute vec3 p; uniform mat4 m; void main(){ gl_Position=m*vec4(p,1.0); }";
static const char *fragment_source="precision mediump float; uniform vec4 c; void main(){ gl_FragColor=c; }";
static tg_context context; static tg_gles_state gl; static tg_gles_draw_state draw_state;
static tg_gles_program_state programs; static unsigned int arena[256];
static GLuint program_id; static int initialized, failed;

static void init(void)
{
    GLuint vs, fs, buffers[2]; GLint status;
    tgGlesInit(&gl,&context); tgGlesMakeCurrent(&gl);
    tgGlesInitDraw(&gl,&draw_state,arena,sizeof(arena),0,0);
    tgGlesInitPrograms(&gl,&programs);
    vs=glCreateShader(GL_VERTEX_SHADER); fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs,1,&vertex_source,0); glCompileShader(vs); glGetShaderiv(vs,GL_COMPILE_STATUS,&status); if(!status) failed=1;
    glShaderSource(fs,1,&fragment_source,0); glCompileShader(fs); glGetShaderiv(fs,GL_COMPILE_STATUS,&status); if(!status) failed=1;
    program_id=glCreateProgram(); glAttachShader(program_id,vs); glAttachShader(program_id,fs);
    glBindAttribLocation(program_id,0,"p"); glLinkProgram(program_id); glGetProgramiv(program_id,GL_LINK_STATUS,&status); if(!status) failed=1;
    glUseProgram(program_id);
    glGenBuffers(2,buffers);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[0]); glBufferData(GL_ARRAY_BUFFER,sizeof(positions),positions,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0); glEnableVertexAttribArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(faces),faces,GL_STATIC_DRAW);
    if(glGetError()!=GL_NO_ERROR) failed=1;
    initialized=1;
}

void cube_render_band(unsigned int *tile, int *depth, int quadrant, unsigned int frame, int row0, int row1)
{
    matrix_t rx,ry,rot,view,mv,projection,mvp; int i,face;
    float sy=sine[frame&31]/256.0f, cy=sine[(frame+8)&31]/256.0f;
    float sx=sine[((frame>>1)+3)&31]/256.0f, cx=sine[((frame>>1)+11)&31]/256.0f;
    context.color=tile; context.depth=depth; context.tile_x=(quadrant&1)*80; context.tile_y=(quadrant>>1)*60;
    if(!initialized) init(); else tgGlesMakeCurrent(&gl);
    glDisable(GL_SCISSOR_TEST);
    if(row0!=0 || row1!=60) { glScissor(context.tile_x,120-context.tile_y-row1,80,row1-row0); glEnable(GL_SCISSOR_TEST); }
    glClearColor(16.0f/255.0f,27.0f/255.0f,43.0f/255.0f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    matrixLoadIdentity(rx); matrixLoadIdentity(ry); matrixLoadIdentity(view);
    rx[5]=cx; rx[6]=sx; rx[9]=-sx; rx[10]=cx;
    ry[0]=cy; ry[2]=-sy; ry[8]=sy; ry[10]=cy; view[14]=-4.5f;
    matrix_multiply(rx,ry,rot); matrix_multiply(view,rot,mv);
    for(i=0;i<16;i++) projection[i]=0.0f;
    projection[0]=1.375f; projection[5]=1.8333333f; projection[10]=-1.0f; projection[11]=-1.0f; projection[14]=-0.2f;
    matrix_multiply(projection,mv,mvp);
    glUniformMatrix4fv(glGetUniformLocation(program_id,"m"),1,GL_FALSE,mvp);
    for(face=0;face<6;face++) {
        glUniform4f(glGetUniformLocation(program_id,"c"),colors[face][0],colors[face][1],colors[face][2],1.0f);
        glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,(const void *)(unsigned long)(face*sizeof(faces[0])));
    }
    if(glGetError()!=GL_NO_ERROR) failed=1;
    if(failed) {
        glDisable(GL_SCISSOR_TEST); glClearColor(1.0f,0.0f,1.0f,1.0f); glClear(GL_COLOR_BUFFER_BIT);
    }
    tgGlesMakeCurrent(0);
}
void cube_render(unsigned int *tile, int *depth, int quadrant, unsigned int frame)
{
    cube_render_band(tile,depth,quadrant,frame,0,60);
}
