#include "gles2_uber_vertex.h"
static float dot(const float *a,const float *b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static int norm(float *v,float (*sqrt_fn)(float))
{
    float length=dot(v,v),s; int i;
    if(!(length>0.0f)) return 0;
    s=sqrt_fn(length); if(!(s>0.0f)) return 0;
    for(i=0;i<3;i++) v[i]/=s;
    return 1;
}
static void transform(const float *m,const float *v,float *out)
{
    int i; for(i=0;i<4;i++) out[i]=m[i]*v[0]+m[i+4]*v[1]+m[i+8]*v[2]+m[i+12];
}
int tg_uber_vertex(const tg_uber_vertex_input *in,const tg_uber_vertex_uniforms *u,tg_uber_vertex_output *out)
{
    tg_uber_vertex_output result; float light[3],view[3],bitangent[3],tmp[3]; int i;
    if(!in || !u || !out || !u->sqrt_fn || (u->features & ~(TG_FRAG_BUMP|TG_FRAG_SPEC|TG_FRAG_SHADOW))) return TG_FRAGMENT_INVALID;
    transform(u->mvp,in->position,result.clip);
    for(i=0;i<3;i++) { light[i]=u->light_position[i]-in->position[i]; view[i]=u->camera_position[i]-in->position[i]; }
    if(u->features&TG_FRAG_BUMP) {
        bitangent[0]=in->normal[1]*in->tangent[2]-in->normal[2]*in->tangent[1];
        bitangent[1]=in->normal[2]*in->tangent[0]-in->normal[0]*in->tangent[2];
        bitangent[2]=in->normal[0]*in->tangent[1]-in->normal[1]*in->tangent[0];
        tmp[0]=dot(light,in->tangent); tmp[1]=dot(light,bitangent); tmp[2]=dot(light,in->normal);
        for(i=0;i<3;i++)light[i]=tmp[i];
        tmp[0]=dot(view,in->tangent); tmp[1]=dot(view,bitangent); tmp[2]=dot(view,in->normal);
        for(i=0;i<3;i++)view[i]=tmp[i];
        if(!norm(view,u->sqrt_fn)) return TG_FRAGMENT_UNDEFINED;
    } else for(i=0;i<3;i++) result.varying.normal[i]=in->normal[i];
    if(!norm(light,u->sqrt_fn)) return TG_FRAGMENT_UNDEFINED;
    for(i=0;i<3;i++) view[i]=(view[i]+light[i])*0.5f;
    if(!norm(view,u->sqrt_fn)) return TG_FRAGMENT_UNDEFINED;
    for(i=0;i<3;i++) { result.varying.light[i]=light[i]; result.varying.half_vector[i]=view[i]; if(u->features&TG_FRAG_BUMP)result.varying.normal[i]=0; }
    result.varying.uv[0]=in->uv[0]; result.varying.uv[1]=in->uv[1];
    if(u->features&TG_FRAG_SHADOW) transform(u->light_mvp,in->position,result.varying.light_clip);
    else for(i=0;i<4;i++) result.varying.light_clip[i]=0;
    *out=result; return TG_FRAGMENT_OK;
}
