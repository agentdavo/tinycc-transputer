#include "gles2_vm_bridge.h"
#include "gles2_texture.h"
#ifndef __transputer__
#include <math.h>
static float root(float x){return sqrtf(x);}
#else
static float root(float x){return __fpsqrt(x);}
#endif
static float integer_pow(float x,float y)
{
    unsigned int n=(unsigned int)y;float result=1;
    while(n){if(n&1)result*=x;x*=x;n>>=1;}return result;
}
static int same(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static const char *attributes[]={"a_vertex","a_normal","a_texcoord0","a_tangent"};
static int bind_varying(tg_gles_program *p,const char *name,int input)
{
    int i=tg_glsl_find_symbol(&p->fragment_ir,name);
    return i<0 || tg_glsl_shader_varying(&p->vm_fragment,name,input);
}
int tgGlesVmLink(tg_gles_program *p)
{
    unsigned int features=0,used=0;int i,j,k;
    if(tg_glsl_find_symbol(&p->vertex_ir,"a_tangent")>=0)features|=TG_FRAG_BUMP;
    if(tg_glsl_find_symbol(&p->vertex_ir,"lightPOVPVMMatrix")>=0)features|=TG_FRAG_SHADOW;
    if(!tg_glsl_uber_vertex_init(&p->vm_vertex,&p->vertex_ir,features))return 0;
    tg_glsl_shader_init(&p->vm_fragment,&p->fragment_ir);p->vm_fragment.vm.pow_fn=integer_pow;
    if(!bind_varying(p,"v_texcoord",TG_GLSL_INPUT_UV)||!bind_varying(p,"v_normal",TG_GLSL_INPUT_NORMAL)||
       !bind_varying(p,"lightVec",TG_GLSL_INPUT_LIGHT)||!bind_varying(p,"halfVec",TG_GLSL_INPUT_HALF)||
       !bind_varying(p,"lightPOVPosition",TG_GLSL_INPUT_LIGHT_CLIP))return 0;
    for(i=0;i<p->fragment_ir.symbol_count;i++)if(p->fragment_ir.symbols[i].kind==TG_GLSL_VARYING && p->vm_fragment.varying_input[i]==TG_GLSL_INPUT_NONE)return 0;
    for(i=0;i<4;i++)p->vm_attributes[i]=255;
    if(p->bound_attribute[0])for(i=0;i<4;i++)if(same(p->bound_attribute,attributes[i])){p->vm_attributes[i]=(unsigned char)p->attribute_index;used|=1u<<p->attribute_index;}
    for(i=0;i<4;i++)if(tg_glsl_find_symbol(&p->vertex_ir,attributes[i])>=0 && p->vm_attributes[i]==255) {
        for(j=0;j<TG_GLES_MAX_ATTRIBUTES && (used&(1u<<j));j++);if(j==TG_GLES_MAX_ATTRIBUTES)return 0;
        p->vm_attributes[i]=(unsigned char)j;used|=1u<<j;
    }
    for(i=0;i<TG_VM_TEXTURES;i++)p->vm_sampler_units[i]=0;
    for(i=0;i<16;i++)p->vm_uniforms.mvp[i]=p->vm_uniforms.light_mvp[i]=0;
    for(i=0;i<3;i++)p->vm_uniforms.light_position[i]=p->vm_uniforms.camera_position[i]=0;
    p->vm_uniforms.features=features;p->vm_uniforms.sqrt_fn=root;
    /* Reject unrecognized attributes rather than accepting and misbinding. */
    for(i=0;i<p->vertex_ir.symbol_count;i++)if(p->vertex_ir.symbols[i].kind==TG_GLSL_ATTRIBUTE){
        for(k=0;k<4;k++)if(same(attributes[k],p->vertex_ir.symbols[i].name))break;if(k==4)return 0;
    }
    return 1;
}
int tgGlesVmAttrib(tg_gles_program *p,const char *name)
{ int i;if(!name)return -1;for(i=0;i<4;i++)if(same(name,attributes[i]))return p->vm_attributes[i]==255?-1:p->vm_attributes[i];return -1; }
int tgGlesVmUniformLocation(tg_gles_program *p,const char *name)
{
    int i;if(!name)return -1;i=tg_glsl_find_symbol(&p->vertex_ir,name);
    if(i>=0 && p->vertex_ir.symbols[i].kind==TG_GLSL_UNIFORM)return i;
    i=tg_glsl_find_symbol(&p->fragment_ir,name);
    if(i>=0 && (p->fragment_ir.symbols[i].kind==TG_GLSL_UNIFORM || p->fragment_ir.symbols[i].kind==TG_GLSL_SAMPLER))return TG_GLSL_SYMBOLS+i;
    return -1;
}
static tg_glsl_symbol *uniform(tg_gles_program *p,int loc,int *vertex)
{
    *vertex=loc<TG_GLSL_SYMBOLS;
    if(loc<0)return 0;
    if(*vertex) { if(loc>=p->vertex_ir.symbol_count)return 0;return &p->vertex_ir.symbols[loc]; }
    loc-=TG_GLSL_SYMBOLS;if(loc>=p->fragment_ir.symbol_count)return 0;return &p->fragment_ir.symbols[loc];
}
int tgGlesVmUniformMatrix(tg_gles_program *p,int loc,const float *v)
{
    int is_vertex,i;tg_glsl_symbol *s=uniform(p,loc,&is_vertex);float *dst;
    if(!s || !is_vertex || s->kind!=TG_GLSL_UNIFORM || s->width!=16)return 0;
    if(same(s->name,"modelViewProjectionMatrix"))dst=p->vm_uniforms.mvp;
    else if(same(s->name,"lightPOVPVMMatrix"))dst=p->vm_uniforms.light_mvp;else return 0;
    for(i=0;i<16;i++)dst[i]=v[i];return 1;
}
int tgGlesVmUniformVector(tg_gles_program *p,int loc,const float *v,int width)
{
    int is_vertex,i;tg_glsl_symbol *s=uniform(p,loc,&is_vertex);float *dst;
    if(!s || s->kind!=TG_GLSL_UNIFORM || s->width!=width)return 0;
    if(is_vertex) {
        if(width!=3)return 0;
        if(same(s->name,"lightPosition"))dst=p->vm_uniforms.light_position;
        else if(same(s->name,"cameraPosition"))dst=p->vm_uniforms.camera_position;else return 0;
        for(i=0;i<width;i++)dst[i]=v[i];return 1;
    }
    if(same(s->name,"materialShininess") && (v[0]<1.0f || v[0]>1024.0f || v[0]!=(float)(unsigned int)v[0]))return 0;
    return tg_glsl_shader_uniform(&p->vm_fragment,s->name,v,width);
}
int tgGlesVmUniformSampler(tg_gles_program *p,int loc,int value)
{
    int is_vertex;tg_glsl_symbol *s=uniform(p,loc,&is_vertex);
    if(!s || is_vertex || s->kind!=TG_GLSL_SAMPLER || value<0 || value>=TG_GLES_TEXTURE_UNITS)return 0;
    p->vm_sampler_units[s->slot]=(unsigned char)value;return 1;
}
static int prepare(void *user)
{
    tg_gles_program *p=user;int i;
    if(!tg_glsl_uber_vertex_uniforms(&p->vm_vertex,&p->vm_uniforms))return 0;
    for(i=0;i<p->fragment_ir.symbol_count;i++)if(p->fragment_ir.symbols[i].kind==TG_GLSL_SAMPLER) {
        tg_glsl_symbol *s=&p->fragment_ir.symbols[i];const tg_fragment_texture *texture=tgGlesTextureUnit(p->vm_sampler_units[s->slot]);
        if(!texture || !tg_glsl_shader_texture(&p->vm_fragment,s->name,texture))return 0;
    }
    return 1;
}
static int shade_vertex(const GLfloat a[TG_GLES_MAX_ATTRIBUTES][4],tg_material_vertex *v,void *user)
{
    tg_gles_program *p=user;tg_uber_vertex_input in;tg_uber_vertex_output out;int i;float w;
    for(i=0;i<3;i++){in.position[i]=a[p->vm_attributes[0]][i];in.normal[i]=a[p->vm_attributes[1]][i];in.tangent[i]=p->vm_attributes[3]==255?0:a[p->vm_attributes[3]][i];}
    for(i=0;i<2;i++)in.uv[i]=a[p->vm_attributes[2]][i];
    if(tg_glsl_uber_vertex_run_prepared(&p->vm_vertex,&in,&out)!=TG_FRAGMENT_OK)return 0;
    w=out.clip[3];if(w<=0.01f || out.clip[0]<-w || out.clip[0]>w || out.clip[1]<-w || out.clip[1]>w || out.clip[2]<-w || out.clip[2]>w)return 0;
    v->x=(int)(80+80*out.clip[0]/w);v->y=(int)(60-60*out.clip[1]/w);
    v->depth=(int)((1.0f-out.clip[2]/w)*16777216.0f);v->inverse_w=1.0f/w;v->varying=out.varying;return 1;
}
static int shade_fragment(void *user,const tg_fragment_varyings *v,float out[4])
{ tg_gles_program *p=user;return tg_glsl_shader_fragment(&p->vm_fragment,v,out); }
void tgGlesVmUse(tg_gles_program *p,tg_gles_draw_state *draw)
{ draw->vertex=0;draw->material_vertex=shade_vertex;draw->material_prepare=prepare;draw->material_fragment=shade_fragment;draw->material_user=p;draw->persistent_cache=GL_FALSE;tgGlesInvalidateVertexCache(draw); }
