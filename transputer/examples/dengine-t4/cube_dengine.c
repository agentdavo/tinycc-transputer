/* Bridge: dEngine's original matrix.c transforms a cube; tinygl rasterizes it.
 * This exercises real upstream code, but is not the dEngine scene renderer. */
#include "../distributed-fb/tinygl.h"
#include "../distributed-fb/gles2_subset.h"
#include "../dEngine/src/matrix.h"

static const signed char positions[8][3] = {
    {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},
    {1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1}
};
static const unsigned char face_positions[24] = {
    0,1,2,3, 4,5,6,7, 1,4,7,2, 5,0,3,6, 3,2,7,6, 5,4,1,0
};
static const unsigned char indices[6] = {0,1,2,0,2,3};
static const unsigned char uv[4][2] = {{0,255},{255,255},{255,0},{0,0}};
static const int sine[32] = {0,50,98,142,181,213,237,251,256,251,237,213,181,142,98,50,0,-50,-98,-142,-181,-213,-237,-251,-256,-251,-237,-213,-181,-142,-98,-50};
static const unsigned int colors[6] = {0xffe46f50u,0xff58aecdu,0xffe0b84au,0xff9d72c9u,0xff6cc28du,0xffd17dadu};

void cube_render_band(unsigned int *tile, int *depth, int quadrant,
                      unsigned int frame, int row0, int row1)
{
    tg_context c; tg_gles_state gl;
    matrix_t rx, ry, rot, view, mv, projection, mvp;
    tg_screen_vertex transformed[8], v[24];
    unsigned int texture[64];
    float sy=sine[frame&31]/256.0f, cy=sine[(frame+8)&31]/256.0f;
    float sx=sine[((frame>>1)+3)&31]/256.0f, cx=sine[((frame>>1)+11)&31]/256.0f;
    int i, face, x, y;
    c.color=tile; c.depth=depth; c.tile_x=(quadrant&1)*80; c.tile_y=(quadrant>>1)*60;
    c.texture=texture;
    tgGlesInit(&gl,&c); tgGlesMakeCurrent(&gl);
    glClearColor(16.0f/255.0f,27.0f/255.0f,43.0f/255.0f,1.0f);
    if (row0!=0 || row1!=60) {
        glScissor(c.tile_x,120-c.tile_y-row1,80,row1-row0);
        glEnable(GL_SCISSOR_TEST);
    }
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

    matrixLoadIdentity(rx); matrixLoadIdentity(ry); matrixLoadIdentity(view);
    rx[5]=cx; rx[6]=sx; rx[9]=-sx; rx[10]=cx;
    ry[0]=cy; ry[2]=-sy; ry[8]=sy; ry[10]=cy;
    view[14]=-4.5f;
    matrix_multiply(rx,ry,rot);
    matrix_multiply(view,rot,mv);
    for (i=0;i<16;i++) projection[i]=0.0f;
    projection[0]=1.375f; projection[5]=1.8333333f;
    projection[10]=-1.0f; projection[11]=-1.0f; projection[14]=-0.2f;
    matrix_multiply(projection,mv,mvp);
    for (i=0;i<8;i++) {
        vec3_t point, ndc; vec4_t in, camera;
        int distance, reciprocal;
        point[0]=(float)positions[i][0]; point[1]=(float)positions[i][1]; point[2]=(float)positions[i][2];
        in[0]=point[0]; in[1]=point[1]; in[2]=point[2]; in[3]=1.0f;
        matrix_transform_vec4t(mv,in,camera);
        matrix_multiplyVertexByMatrix(point,mvp,ndc);
        distance=(int)(-camera[2]*256.0f);
        reciprocal=262144/distance;
        transformed[i].x=80+(int)(ndc[0]*80.0f);
        transformed[i].y=60-(int)(ndc[1]*60.0f);
        transformed[i].z=reciprocal;
    }
    for (i=0;i<24;i++) {
        int q;
        v[i]=transformed[face_positions[i]]; q=v[i].z;
        v[i].u=(uv[i&3][0]*q)>>8; v[i].v=(uv[i&3][1]*q)>>8;
    }
    for (face=0;face<6;face++) {
        for (y=0;y<8;y++) for (x=0;x<8;x++) {
            unsigned int base=colors[face];
            texture[y*8+x]=((x/2+y/2)&1) ? base : ((base & 0x00fefefeu)>>1)|0xff000000u;
        }
        tg_draw_elements(&c,v+face*4,indices,2);
    }
    tgGlesMakeCurrent(0);
}
void cube_render(unsigned int *tile, int *depth, int quadrant, unsigned int frame)
{
    cube_render_band(tile,depth,quadrant,frame,0,60);
}
