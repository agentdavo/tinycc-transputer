#include "material_raster.h"

static int edge(int ax,int ay,int bx,int by,int x,int y)
{ return (x-ax)*(by-ay)-(y-ay)*(bx-ax); }

static int triangle(tg_context *c,const tg_material_vertex *a,
                         const tg_material_vertex *b,const tg_material_vertex *d,
                         const tg_fragment_material *material,int blended,tg_material_fragment_fn fragment,void *user,const tg_material_draw_options *options)
{
    int area=edge(a->x,a->y,b->x,b->y,d->x,d->y);
    int x0,x1,y0,y1,x,y,i,errors=0,row0,row1,stepx0,stepx1,stepy0,stepy1;
    if(!area || a->inverse_w<=0 || b->inverse_w<=0 || d->inverse_w<=0) return 0;
    if(options) {
        if(options->cull==3 || (options->cull==1 && area<0) || (options->cull==2 && area>0))return 0;
        if(area<0){const tg_material_vertex *t=b;b=d;d=t;area=-area;}
    } else if(area<0)return 0;
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
    row0=edge(b->x,b->y,d->x,d->y,x0,y0); row1=edge(d->x,d->y,a->x,a->y,x0,y0);
    stepx0=d->y-b->y; stepx1=a->y-d->y;
    stepy0=b->x-d->x; stepy1=d->x-a->x;
    for(y=y0;y<=y1;y++,row0+=stepy0,row1+=stepy1) {
      int e0=row0,e1=row1;
      for(x=x0;x<=x1;x++,e0+=stepx0,e1+=stepx1) {
        int e2=area-e0-e1;
        int off,depth; float p,q,r,sum,out[4]; tg_fragment_varyings v;
        if(e0<0 || e1<0 || e2<0) continue;
        off=(y-c->tile_y)*TG_TILE_W+x-c->tile_x;
        /* Depth is already in window coordinates and is linear on screen. */
        depth=(int)(((float)e0*a->depth+(float)e1*b->depth+(float)e2*d->depth)/(float)area);
        if((!options || options->depth_test) && depth<=c->depth[off]) continue;
        p=(float)e0*a->inverse_w; q=(float)e1*b->inverse_w; r=(float)e2*d->inverse_w;
        sum=p+q+r; if(sum<=0.0f) continue; p/=sum; q/=sum; r/=sum;
#define INTERP(field,n) for(i=0;i<(n);i++) v.field[i]=p*a->varying.field[i]+q*b->varying.field[i]+r*d->varying.field[i]
        INTERP(light,3); INTERP(half_vector,3); INTERP(normal,3); INTERP(uv,2); INTERP(light_clip,4);
#undef INTERP
        if((fragment ? fragment(user,&v,out) : tg_fragment_uber(material,&v,out))!=TG_FRAGMENT_OK) { errors++; continue; }
        if(blended || (options && options->blend)) {
            unsigned int src=tg_fragment_argb(out),dst=c->color[off],alpha=src>>24,inv=255-alpha;
            unsigned int r=(((src>>16)&255)*alpha+((dst>>16)&255)*inv+127)/255;
            unsigned int g=(((src>>8)&255)*alpha+((dst>>8)&255)*inv+127)/255;
            unsigned int b=((src&255)*alpha+(dst&255)*inv+127)/255;
            unsigned int da=options?((alpha*alpha+(dst>>24)*inv+127)/255):(alpha+((dst>>24)*inv+127)/255);
            unsigned int color=(da<<24)|(r<<16)|(g<<8)|b;
            c->color[off]=options?((dst&~options->color_mask)|(color&options->color_mask)):color;
            if(options && options->depth_test && options->depth_write)c->depth[off]=depth;
        } else {
            unsigned int color=tg_fragment_argb(out);
            if(!options || (options->depth_test && options->depth_write))c->depth[off]=depth;
            c->color[off]=options?((c->color[off]&~options->color_mask)|(color&options->color_mask)):color;
        }
      }
    }
    return errors;
}
int tg_material_triangle(tg_context *c,const tg_material_vertex *a,const tg_material_vertex *b,const tg_material_vertex *d,const tg_fragment_material *m)
{ return triangle(c,a,b,d,m,0,0,0,0); }
int tg_material_triangle_blended(tg_context *c,const tg_material_vertex *a,const tg_material_vertex *b,const tg_material_vertex *d,const tg_fragment_material *m)
{ return triangle(c,a,b,d,m,1,0,0,0); }
int tg_material_triangle_custom(tg_context *c,const tg_material_vertex *a,const tg_material_vertex *b,const tg_material_vertex *d,tg_material_fragment_fn fn,void *user)
{ if(!fn)return 1;return triangle(c,a,b,d,0,0,fn,user,0); }
int tg_material_triangle_config(tg_context *c,const tg_material_vertex *a,const tg_material_vertex *b,const tg_material_vertex *d,tg_material_fragment_fn fn,void *user,const tg_material_draw_options *options)
{ if(!fn||!options)return 1;return triangle(c,a,b,d,0,0,fn,user,options); }
