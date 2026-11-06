/* Original OBJ room, transformed by the source map's matrix at export time.
 * Clip in view space before projection; all inputs are bounded scene assets. */
#include "demo_room.h"
#include "room_asset.h"
#define MAX_ROOM_TRIANGLES (ROOM_TRIANGLE_COUNT*2)
typedef struct { float x,y,z,nx,ny,nz,u,v; } clip_vertex;
typedef struct { tg_material_vertex v[3]; unsigned short texture; } draw_triangle;
static draw_triangle projected[MAX_ROOM_TRIANGLES];
static unsigned int projected_count;
static tg_fragment_texture textures[ROOM_TEXTURE_COUNT];
static float cx,cy,cz;
static float square(float x,float exponent) { (void)exponent; return x*x; }
static clip_vertex intersect(clip_vertex a,clip_vertex b)
{
    clip_vertex o; float t=(cz-0.2f-a.z)/(b.z-a.z);
#define LERP(f) o.f=a.f+(b.f-a.f)*t
    LERP(x); LERP(y); LERP(z); LERP(nx); LERP(ny); LERP(nz); LERP(u); LERP(v);
#undef LERP
    return o;
}
static void project(tg_material_vertex *v,clip_vertex p)
{
    float w=cz-p.z;
    v->x=(int)(80.0f+72.0f*(p.x-cx)/w); v->y=(int)(60.0f-72.0f*(p.y-cy)/w);
    v->depth=(int)(100000.0f/w); v->inverse_w=1.0f/w;
    v->varying.uv[0]=p.u; v->varying.uv[1]=p.v;
    v->varying.normal[0]=p.nx; v->varying.normal[1]=p.ny; v->varying.normal[2]=p.nz;
    v->varying.light[0]=v->varying.light[1]=0; v->varying.light[2]=1;
    v->varying.half_vector[0]=v->varying.half_vector[1]=0; v->varying.half_vector[2]=1;
    v->varying.light_clip[0]=p.x; v->varying.light_clip[1]=p.y;
    v->varying.light_clip[2]=-p.z; v->varying.light_clip[3]=1;
}
void demo_room_init(float center_x,float center_y,float center_z,float scale,float camera_x,float camera_y,float camera_z)
{
    unsigned int i,j; cx=camera_x; cy=camera_y; cz=camera_z; projected_count=0;
    for(i=0;i<ROOM_TEXTURE_COUNT;i++) { textures[i].rgba=room_textures[i]; textures[i].width=textures[i].height=16; textures[i].wrap_s=textures[i].wrap_t=TG_FRAG_REPEAT; }
    for(i=0;i<ROOM_TRIANGLE_COUNT;i++) {
        clip_vertex in[3],out[4]; unsigned int n=0; const room_source_triangle *tri=&room_triangles[i];
        unsigned short indexes[3]; indexes[0]=tri->a; indexes[1]=tri->b; indexes[2]=tri->c;
        for(j=0;j<3;j++) {
            const room_source_vertex *s=&room_vertices[indexes[j]]; clip_vertex *v=&in[j];
            v->x=(s->x-center_x)*scale; v->y=(s->y-center_y)*scale; v->z=(s->z-center_z)*scale;
            v->nx=s->nx; v->ny=s->ny; v->nz=s->nz; v->u=s->u; v->v=s->v;
        }
        for(j=0;j<3;j++) {
            clip_vertex a=in[j],b=in[(j+1)%3]; int a_in=(cz-a.z>=0.2f),b_in=(cz-b.z>=0.2f);
            if(a_in && b_in) out[n++]=b;
            else if(a_in && !b_in) out[n++]=intersect(a,b);
            else if(!a_in && b_in) { out[n++]=intersect(a,b); out[n++]=b; }
        }
        for(j=1;j+1<n && projected_count<MAX_ROOM_TRIANGLES;j++) {
            draw_triangle *d=&projected[projected_count++];
            project(&d->v[0],out[0]); project(&d->v[1],out[j]); project(&d->v[2],out[j+1]); d->texture=tri->texture;
        }
    }
}
int demo_room_draw(tg_context *ctx,const tg_fragment_texture *shadow)
{
    static tg_fragment_material m; unsigned int i; int errors=0;
    m.shadow=shadow; m.features=TG_FRAG_SHADOW; m.pow_fn=square; m.shininess=2; m.clamp_specular_base=1;
    for(i=0;i<3;i++) { m.ambient[i]=0.5f; m.diffuse[i]=0.5f; }
    for(i=0;i<projected_count;i++) {
        draw_triangle *t=&projected[i]; m.base=&textures[t->texture];
        errors+=tg_material_triangle(ctx,&t->v[0],&t->v[1],&t->v[2],&m);
    }
    return errors;
}
