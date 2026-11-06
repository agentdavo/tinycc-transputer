#include "material_raster.h"
static float distance_to_plane(const tg_material_clip_vertex *v,int plane)
{
    int axis=plane/2;return v->clip[3]+((plane&1)?-v->clip[axis]:v->clip[axis]);
}
static void interpolate(tg_material_clip_vertex *out,const tg_material_clip_vertex *a,const tg_material_clip_vertex *b,float t)
{
    int i;for(i=0;i<4;i++)out->clip[i]=a->clip[i]+(b->clip[i]-a->clip[i])*t;
#define LERP(field,n) for(i=0;i<(n);i++)out->varying.field[i]=a->varying.field[i]+(b->varying.field[i]-a->varying.field[i])*t
    LERP(light,3);LERP(half_vector,3);LERP(normal,3);LERP(uv,2);LERP(light_clip,4);
#undef LERP
}
int tg_material_clip_triangle(tg_context *ctx,const tg_material_clip_vertex *a,const tg_material_clip_vertex *b,const tg_material_clip_vertex *c,
                              tg_material_fragment_fn fn,void *user,const tg_material_draw_options *options,
                              int viewport_x,int viewport_y,int viewport_width,int viewport_height,int logical_height)
{
    tg_material_clip_vertex p[12],q[12];tg_material_vertex projected[12];int n=3,plane,i,j,errors=0;
    if(!ctx||!a||!b||!c||!fn||!options)return 1;p[0]=*a;p[1]=*b;p[2]=*c;
    for(plane=0;plane<6 && n;plane++) {
        int count=0;
        for(i=0;i<n;i++) {
            const tg_material_clip_vertex *v=&p[i],*next=&p[(i+1)%n];
            float da=distance_to_plane(v,plane),db=distance_to_plane(next,plane);int ina=da>=0,inb=db>=0;
            if(ina&&inb){if(count>=12)return 1;q[count++]=*next;}
            else if(ina&&!inb){if(count>=12)return 1;interpolate(&q[count++],v,next,da/(da-db));}
            else if(!ina&&inb){if(count+1>=12)return 1;interpolate(&q[count++],v,next,da/(da-db));q[count++]=*next;}
        }
        n=count;for(i=0;i<n;i++)p[i]=q[i];
    }
    if(n<3)return 0;
    for(i=0;i<n;i++) {
        float w=p[i].clip[3];if(!(w>0.000001f))return 0;
        projected[i].x=(int)(viewport_x+(1.0f+p[i].clip[0]/w)*0.5f*viewport_width);
        projected[i].y=(int)(logical_height-viewport_y-(1.0f+p[i].clip[1]/w)*0.5f*viewport_height);
        projected[i].depth=(int)((1.0f-p[i].clip[2]/w)*16777216.0f);
        projected[i].inverse_w=1.0f/w;projected[i].varying=p[i].varying;
    }
    for(j=1;j+1<n;j++)errors+=tg_material_triangle_config(ctx,&projected[0],&projected[j],&projected[j+1],fn,user,options);
    return errors;
}
