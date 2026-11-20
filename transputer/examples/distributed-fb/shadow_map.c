#include "shadow_map.h"
static int edge(int ax,int ay,int bx,int by,int x,int y)
{ return (x-ax)*(by-ay)-(y-ay)*(bx-ax); }
static float fract(float x) { return x-(float)(int)x; }
static unsigned char byte(float x)
{ if(x<=0)return 0; if(x>=1)return 255; return (unsigned char)(x*255.0f+0.5f); }
static void pack(float z,unsigned char *out)
{
    float a=fract(z*16777216.0f),b=fract(z*65536.0f),c=fract(z*256.0f),d=fract(z);
    out[0]=byte(a); out[1]=byte(b-a/256.0f); out[2]=byte(c-b/256.0f); out[3]=byte(d-c/256.0f);
}
int tg_shadow_clear(tg_shadow_map *m)
{
    unsigned int i,n;
    if(!m || !m->depth || !m->rgba || !m->width || !m->height || m->width>4096 || m->height>4096) return 0;
    n=m->width*m->height;
    for(i=0;i<n;i++) { m->depth[i]=1.0f; m->rgba[i*4]=m->rgba[i*4+1]=m->rgba[i*4+2]=m->rgba[i*4+3]=255; }
    return 1;
}
void tg_shadow_triangle(tg_shadow_map *m,const tg_shadow_vertex *a,const tg_shadow_vertex *b,const tg_shadow_vertex *c)
{
    int area=edge(a->x,a->y,b->x,b->y,c->x,c->y),x0,x1,y0,y1,x,y;
    if(!m || !m->depth || !m->rgba || !m->width || !m->height || m->width>4096 || m->height>4096 || !area) return;
    if(area<0) { const tg_shadow_vertex *t=b; b=c; c=t; area=-area; }
    x0=a->x<b->x?a->x:b->x; if(c->x<x0)x0=c->x;
    x1=a->x>b->x?a->x:b->x; if(c->x>x1)x1=c->x;
    y0=a->y<b->y?a->y:b->y; if(c->y<y0)y0=c->y;
    y1=a->y>b->y?a->y:b->y; if(c->y>y1)y1=c->y;
    if(x0<0)x0=0; if(y0<0)y0=0; if(x1>=(int)m->width)x1=m->width-1; if(y1>=(int)m->height)y1=m->height-1;
    for(y=y0;y<=y1;y++) for(x=x0;x<=x1;x++) {
        int e0=edge(b->x,b->y,c->x,c->y,x,y),e1=edge(c->x,c->y,a->x,a->y,x,y),e2=area-e0-e1;
        float z; unsigned int off;
        if(e0<0 || e1<0 || e2<0)continue;
        z=((float)e0*a->depth+(float)e1*b->depth+(float)e2*c->depth)/(float)area;
        if(z<0.0f || z>=1.0f)continue;
        off=(unsigned int)y*m->width+(unsigned int)x;
        if(z<m->depth[off]) { m->depth[off]=z; pack(z,m->rgba+off*4); }
    }
}
