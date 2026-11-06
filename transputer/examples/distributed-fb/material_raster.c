#include "material_raster.h"

static int edge(int ax,int ay,int bx,int by,int x,int y)
{ return (x-ax)*(by-ay)-(y-ay)*(bx-ax); }

int tg_material_triangle(tg_context *c,const tg_material_vertex *a,
                         const tg_material_vertex *b,const tg_material_vertex *d,
                         const tg_fragment_material *material)
{
    int area=edge(a->x,a->y,b->x,b->y,d->x,d->y);
    int x0,x1,y0,y1,x,y,i,errors=0;
    if(area<=0 || a->inverse_w<=0 || b->inverse_w<=0 || d->inverse_w<=0) return 0;
    x0=a->x<b->x?a->x:b->x; if(d->x<x0)x0=d->x;
    x1=a->x>b->x?a->x:b->x; if(d->x>x1)x1=d->x;
    y0=a->y<b->y?a->y:b->y; if(d->y<y0)y0=d->y;
    y1=a->y>b->y?a->y:b->y; if(d->y>y1)y1=d->y;
    if(x0<c->tile_x)x0=c->tile_x; if(x1>=c->tile_x+TG_TILE_W)x1=c->tile_x+TG_TILE_W-1;
    if(y0<c->tile_y)y0=c->tile_y; if(y1>=c->tile_y+TG_TILE_H)y1=c->tile_y+TG_TILE_H-1;
    if(c->clip_enabled) {
        if(x0<c->clip_x0)x0=c->clip_x0; if(x1>=c->clip_x1)x1=c->clip_x1-1;
        if(y0<c->clip_y0)y0=c->clip_y0; if(y1>=c->clip_y1)y1=c->clip_y1-1;
    }
    for(y=y0;y<=y1;y++) for(x=x0;x<=x1;x++) {
        int e0=edge(b->x,b->y,d->x,d->y,x,y),e1=edge(d->x,d->y,a->x,a->y,x,y),e2=area-e0-e1;
        int off,depth; float p,q,r,sum,out[4]; tg_fragment_varyings v;
        if(e0<0 || e1<0 || e2<0) continue;
        off=(y-c->tile_y)*TG_TILE_W+x-c->tile_x;
        /* Depth is already in window coordinates and is linear on screen. */
        depth=(int)(((float)e0*a->depth+(float)e1*b->depth+(float)e2*d->depth)/(float)area);
        if(depth<=c->depth[off]) continue;
        p=(float)e0*a->inverse_w; q=(float)e1*b->inverse_w; r=(float)e2*d->inverse_w;
        sum=p+q+r; if(sum<=0.0f) continue; p/=sum; q/=sum; r/=sum;
#define INTERP(field,n) for(i=0;i<(n);i++) v.field[i]=p*a->varying.field[i]+q*b->varying.field[i]+r*d->varying.field[i]
        INTERP(light,3); INTERP(half_vector,3); INTERP(normal,3); INTERP(uv,2); INTERP(light_clip,4);
#undef INTERP
        if(tg_fragment_uber(material,&v,out)!=TG_FRAGMENT_OK) { errors++; continue; }
        c->depth[off]=depth; c->color[off]=tg_fragment_argb(out);
    }
    return errors;
}
