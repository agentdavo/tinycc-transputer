#include "tinygl.h"
#include "gles2_subset.h"
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

void cube_render(unsigned int *tile, int *depth, int node, unsigned int frame)
{
    tg_context c; tg_gles_state gl; tg_screen_vertex v[24]; unsigned int texture[64];
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
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    /* The 24 attribute vertices share eight positions. Transform each
     * position only once, then retain each face's independent UVs. */
    tg_transform(&c,vertices,v,8);
    for (x=8;x<24;++x) {
        int from=position_index[x-8];
        v[x].x=v[from].x; v[x].y=v[from].y; v[x].z=v[from].z;
        v[x].u=vertices[x].u; v[x].v=vertices[x].v;
    }
    for (face=0;face<6;++face) {
        for (y=0;y<8;++y) for (x=0;x<8;++x) {
            unsigned int base=colors[face];
            texture[y*8+x]=((x/2+y/2)&1) ? base : ((base & 0x00fefefeu)>>1)|0xff000000u;
        }
        tg_draw_elements(&c,v+face*4,triangles,2);
    }
    tgGlesMakeCurrent(0);
}
