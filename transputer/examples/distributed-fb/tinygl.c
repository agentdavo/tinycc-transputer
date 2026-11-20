#include "tinygl.h"
#ifdef TG_GLES_FRAMEBUFFER
#define WIDTH(c) ((c)->surface_width)
#define HEIGHT(c) ((c)->surface_height)
#define PITCH(c) ((c)->surface_pitch)
#else
#define WIDTH(c) TG_TILE_W
#define HEIGHT(c) TG_TILE_H
#define PITCH(c) TG_TILE_W
#endif
#if defined(__transputer__) && !defined(TG_SCALAR_CLEAR)
void __move(void *dest, const void *src, int length);
static void fill_words(unsigned int *dst, unsigned int value, int n)
{
    int filled=1, count;
    dst[0]=value;
    while (filled<n) {
        count=filled;
        if (count>n-filled) count=n-filled;
        __move(dst+filled,dst,count*4);
        filled+=count;
    }
}
#endif

void tg_clear_band(tg_context *c, unsigned int color, int row0, int row1)
{
    int i, start, count;
    if (row0<0) row0=0;
    if (row1>HEIGHT(c)) row1=HEIGHT(c);
    if (row0>=row1) return;
    start=row0*PITCH(c); count=(row1-row0)*WIDTH(c);
#ifdef TG_GLES_FRAMEBUFFER
    if(PITCH(c)!=WIDTH(c)) {
        int y,x;for(y=row0;y<row1;y++)for(x=0;x<WIDTH(c);x++){i=y*PITCH(c)+x;c->color[i]=color;c->depth[i]=-2147483647;}return;
    }
#endif
#if defined(__transputer__) && !defined(TG_SCALAR_CLEAR)
    fill_words(c->color+start,color,count);
    fill_words((unsigned int *)c->depth+start,0x80000001u,count);
#else
    for (i = start; i < start+count; ++i) {
        c->color[i] = color;
        c->depth[i] = -2147483647;
    }
#endif
}
void tg_clear(tg_context *c, unsigned int color)
{
    tg_clear_band(c,color,0,HEIGHT(c));
}

void tg_transform(const tg_context *c, const tg_vertex *src, tg_screen_vertex *dst, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        int x = src[i].x, y = src[i].y, z = src[i].z;
        int rx = (c->matrix[0]*x + c->matrix[1]*y + c->matrix[2]*z) >> 8;
        int ry = (c->matrix[3]*x + c->matrix[4]*y + c->matrix[5]*z) >> 8;
        int rz = (c->matrix[6]*x + c->matrix[7]*y + c->matrix[8]*z) >> 8;
        int distance = c->camera_distance - rz;
        int reciprocal = 262144 / distance;
        dst[i].x = 80 + rx * c->focal_length / distance;
        dst[i].y = 60 - ry * c->focal_length / distance;
        dst[i].z = reciprocal;
        dst[i].u = (src[i].u * reciprocal) >> 8;
        dst[i].v = (src[i].v * reciprocal) >> 8;
    }
}

static int edge(int ax, int ay, int bx, int by, int x, int y)
{
    return (x - ax) * (by - ay) - (y - ay) * (bx - ax);
}

/* Perspective-correct: step 1/z and u/z, v/z; divide at covered pixels.
 * Geometry must be wholly in front of the near plane (no clipping here). */
static void triangle(tg_context *c, tg_screen_vertex a, tg_screen_vertex b, tg_screen_vertex d)
{
    int area = edge(a.x,a.y,b.x,b.y,d.x,d.y);
    int x0, x1, y0, y1, x, y, anchor_x, anchor_y, dx, dy;
    int w0, w1, w2, wx0, wx1, wx2, wy0, wy1, wy2;
    int zrow, urow, vrow, zx, ux, vx, zy, uy, vy;
    if (!area) return;
#ifndef TG_NO_CULL
    /* Cube faces have outward winding; negative screen area faces away. */
    if (area < 0) return;
#else
    if (area < 0) { tg_screen_vertex t = b; b = d; d = t; area = -area; }
#endif
    x0 = a.x < b.x ? a.x : b.x; if (d.x < x0) x0 = d.x;
    x1 = a.x > b.x ? a.x : b.x; if (d.x > x1) x1 = d.x;
    y0 = a.y < b.y ? a.y : b.y; if (d.y < y0) y0 = d.y;
    y1 = a.y > b.y ? a.y : b.y; if (d.y > y1) y1 = d.y;
    if (x0 < c->tile_x) x0 = c->tile_x;
    if (y0 < c->tile_y) y0 = c->tile_y;
    if (x1 >= c->tile_x + WIDTH(c)) x1 = c->tile_x + WIDTH(c) - 1;
    if (y1 >= c->tile_y + HEIGHT(c)) y1 = c->tile_y + HEIGHT(c) - 1;
    anchor_x=x0; anchor_y=y0;
    if (c->clip_enabled) {
        if (x0 < c->clip_x0) x0=c->clip_x0;
        if (y0 < c->clip_y0) y0=c->clip_y0;
        if (x1 >= c->clip_x1) x1=c->clip_x1-1;
        if (y1 >= c->clip_y1) y1=c->clip_y1-1;
    }
    if (x0 > x1 || y0 > y1) return;
    wx0=d.y-b.y; wx1=a.y-d.y; wx2=b.y-a.y;
    wy0=b.x-d.x; wy1=d.x-a.x; wy2=a.x-b.x;
    w0=edge(b.x,b.y,d.x,d.y,anchor_x,anchor_y); w1=edge(d.x,d.y,a.x,a.y,anchor_x,anchor_y); w2=area-w0-w1;
#define GRAD(q,wA,wB,wC) (((wA)*a.q+(wB)*b.q+(wC)*d.q)*256/area)
    zx=GRAD(z,wx0,wx1,wx2); zy=GRAD(z,wy0,wy1,wy2);
    ux=GRAD(u,wx0,wx1,wx2); uy=GRAD(u,wy0,wy1,wy2);
    vx=GRAD(v,wx0,wx1,wx2); vy=GRAD(v,wy0,wy1,wy2);
    zrow=GRAD(z,w0,w1,w2); urow=GRAD(u,w0,w1,w2); vrow=GRAD(v,w0,w1,w2);
#undef GRAD
    dx=x0-anchor_x; dy=y0-anchor_y;
    w0+=wx0*dx+wy0*dy; w1+=wx1*dx+wy1*dy; w2+=wx2*dx+wy2*dy;
    zrow+=zx*dx+zy*dy; urow+=ux*dx+uy*dy; vrow+=vx*dx+vy*dy;
    for (y=y0; y<=y1; ++y) {
        int e0=w0,e1=w1,e2=w2, z=zrow,u=urow,v=vrow;
        int off=(y-c->tile_y)*PITCH(c)+x0-c->tile_x;
        for (x=x0; x<=x1; ++x,++off) {
            if (e0>=0 && e1>=0 && e2>=0 && z>c->depth[off]) {
                int q=z>>8;
                int tu=q>0 ? (u>>8)*8/q : 0;
                int tv=q>0 ? (v>>8)*8/q : 0;
                if (tu<0) tu=0; else if (tu>7) tu=7;
                if (tv<0) tv=0; else if (tv>7) tv=7;
                c->depth[off]=z;
                c->color[off]=c->texture[tv*8+tu];
            }
            e0+=wx0; e1+=wx1; e2+=wx2; z+=zx; u+=ux; v+=vx;
        }
        w0+=wy0; w1+=wy1; w2+=wy2; zrow+=zy; urow+=uy; vrow+=vy;
    }
}

void tg_draw_elements(tg_context *c, const tg_screen_vertex *v, const unsigned char *idx, int triangles)
{
    int i;
    for (i=0; i<triangles; ++i) triangle(c,v[idx[i*3]],v[idx[i*3+1]],v[idx[i*3+2]]);
}
void tg_draw_triangle(tg_context *c, const tg_screen_vertex *a, const tg_screen_vertex *b, const tg_screen_vertex *d)
{
    triangle(c,*a,*b,*d);
}

unsigned int tg_checksum(const unsigned int *p, int n)
{
    unsigned int h=2166136261u; int i;
    for (i=0;i<n;++i) h=(h^p[i])*16777619u;
    return h;
}
