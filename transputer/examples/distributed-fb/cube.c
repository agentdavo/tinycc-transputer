#include "tinygl.h"
#include "gles2_subset.h"
#ifdef TG_GLES_DRAW
#include "gles2_draw.h"
static int cube_vertex(const GLfloat attr[TG_GLES_MAX_ATTRIBUTES][4], tg_screen_vertex *out, void *user)
{
    (void)user;
    out->x=(int)attr[0][0]; out->y=(int)attr[0][1]; out->z=(int)attr[0][2];
    out->u=(int)attr[1][0]; out->v=(int)attr[1][1];
    return 1;
}
#endif
#ifndef CAMERA_DISTANCE
#define CAMERA_DISTANCE 1152
#endif
#ifndef FOCAL_LENGTH
#define FOCAL_LENGTH 110
#endif

/* 24 vertices retain distinct texture coordinates on each cube face. */
#define V(x,y,z,u,v) {x,y,z,u,v}
static const tg_vertex vertices[24] = {
 V(-256,-256, 256,0,255), V( 256,-256, 256,255,255), V( 256, 256, 256,255,0), V(-256, 256, 256,0,0),
 V( 256,-256,-256,0,255), V(-256,-256,-256,255,255), V(-256, 256,-256,255,0), V( 256, 256,-256,0,0),
 V( 256,-256, 256,0,255), V( 256,-256,-256,255,255), V( 256, 256,-256,255,0), V( 256, 256, 256,0,0),
 V(-256,-256,-256,0,255), V(-256,-256, 256,255,255), V(-256, 256, 256,255,0), V(-256, 256,-256,0,0),
 V(-256, 256, 256,0,255), V( 256, 256, 256,255,255), V( 256, 256,-256,255,0), V(-256, 256,-256,0,0),
 V(-256,-256,-256,0,255), V( 256,-256,-256,255,255), V( 256,-256, 256,255,0), V(-256,-256, 256,0,0)
};
static const unsigned char triangles[6] = { 0,1,2, 0,2,3 };
static const unsigned char position_index[16] = {1,4,7,2, 5,0,3,6, 3,2,7,6, 5,4,1,0};
static const int sine[32] = {0,50,98,142,181,213,237,251,256,251,237,213,181,142,98,50,0,-50,-98,-142,-181,-213,-237,-251,-256,-251,-237,-213,-181,-142,-98,-50};
static const unsigned int colors[6] = {0xffe46f50u,0xff58aecdu,0xffe0b84au,0xff9d72c9u,0xff6cc28du,0xffd17dadu};

void cube_render_band(unsigned int *tile, int *depth, int node, unsigned int frame, int row0, int row1)
{
    tg_context c; tg_gles_state gl; tg_screen_vertex v[24]; unsigned int texture[64];
#ifdef TG_GLES_DRAW
    tg_gles_draw_state ds;
    GLfloat input[24][5];
    static const unsigned short face_indices[6]={0,1,2,0,2,3};
#endif
    int sy=sine[frame&31], cy=sine[(frame+8)&31];
    int sx=sine[((frame>>1)+3)&31], cx=sine[((frame>>1)+11)&31];
    int face, x, y;
    c.color=tile; c.depth=depth; c.tile_x=(node&1)*TG_TILE_W; c.tile_y=(node>>1)*TG_TILE_H;
    c.texture=texture;
    c.camera_distance=CAMERA_DISTANCE; c.focal_length=FOCAL_LENGTH;
    c.matrix[0]=cy; c.matrix[1]=0; c.matrix[2]=sy;
    c.matrix[3]=(sx*sy)>>8; c.matrix[4]=cx; c.matrix[5]=-(sx*cy>>8);
    c.matrix[6]=-(cx*sy>>8); c.matrix[7]=sx; c.matrix[8]=(cx*cy)>>8;
    tgGlesInit(&gl,&c); tgGlesMakeCurrent(&gl);
    glClearColor(16.0f/255.0f,27.0f/255.0f,43.0f/255.0f,1.0f);
#ifdef TG_FULL_CLEAR_BANDS
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
#endif
    if (row0!=0 || row1!=TG_TILE_H) {
        glScissor(c.tile_x,120-c.tile_y-row1,TG_TILE_W,row1-row0);
        glEnable(GL_SCISSOR_TEST);
    }
#ifndef TG_FULL_CLEAR_BANDS
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
#endif
    /* The 24 attribute vertices share eight positions. Transform each
     * position only once, then retain each face's independent UVs. */
    tg_transform(&c,vertices,v,8);
    for (x=8;x<24;++x) {
        int from=position_index[x-8];
        v[x].x=v[from].x; v[x].y=v[from].y; v[x].z=v[from].z;
        v[x].u=vertices[x].u; v[x].v=vertices[x].v;
    }
#ifdef TG_GLES_DRAW
    tgGlesInitDraw(&gl,&ds,0,0,cube_vertex,0);
    for (x=0;x<24;x++) {
        input[x][0]=(GLfloat)v[x].x; input[x][1]=(GLfloat)v[x].y; input[x][2]=(GLfloat)v[x].z;
        input[x][3]=(GLfloat)v[x].u; input[x][4]=(GLfloat)v[x].v;
    }
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
#endif
    for (face=0;face<6;++face) {
        for (y=0;y<8;++y) for (x=0;x<8;++x) {
            unsigned int base=colors[face];
            texture[y*8+x]=((x/2+y/2)&1) ? base : ((base & 0x00fefefeu)>>1)|0xff000000u;
        }
#ifdef TG_GLES_DRAW
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(input[0]),&input[face*4][0]);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(input[0]),&input[face*4][3]);
        glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,face_indices);
#else
        tg_draw_elements(&c,v+face*4,triangles,2);
#endif
    }
    tgGlesMakeCurrent(0);
}
void cube_render(unsigned int *tile, int *depth, int node, unsigned int frame)
{
    cube_render_band(tile,depth,node,frame,0,TG_TILE_H);
}
