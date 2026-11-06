#include "glsl_uber_vertex_shader.h"
static int slot(const tg_glsl_fragment_program *p,const char *name,int kind,int width)
{ int i=tg_glsl_find_symbol(p,name);if(i<0||p->symbols[i].kind!=kind||p->symbols[i].width!=width)return -1;return p->symbols[i].slot; }
int tg_glsl_uber_vertex_init(tg_glsl_uber_vertex_shader *s,const tg_glsl_fragment_program *p,unsigned int features)
{
    int x;if(!s||!p||(features&~7u))return 0;s->program=p;s->features=features;tg_glsl_fragment_init(p,&s->vm);
#define BIND(field,name,kind,width) do{x=slot(p,name,kind,width);if(x<0)return 0;s->field=(unsigned char)x;}while(0)
    BIND(position,"a_vertex",TG_GLSL_ATTRIBUTE,3);BIND(normal,"a_normal",TG_GLSL_ATTRIBUTE,3);
    BIND(uv,"a_texcoord0",TG_GLSL_ATTRIBUTE,2);BIND(mvp,"modelViewProjectionMatrix",TG_GLSL_UNIFORM,16);
    BIND(light_pos,"lightPosition",TG_GLSL_UNIFORM,3);BIND(camera_pos,"cameraPosition",TG_GLSL_UNIFORM,3);
    BIND(out_light,"lightVec",TG_GLSL_VARYING,3);BIND(out_half,"halfVec",TG_GLSL_VARYING,3);BIND(out_uv,"v_texcoord",TG_GLSL_VARYING,2);
    if(features&TG_FRAG_BUMP){BIND(tangent,"a_tangent",TG_GLSL_ATTRIBUTE,3);}
    else {BIND(out_normal,"v_normal",TG_GLSL_VARYING,3);}
    if(features&TG_FRAG_SHADOW){BIND(light_mvp,"lightPOVPVMMatrix",TG_GLSL_UNIFORM,16);BIND(out_light_clip,"lightPOVPosition",TG_GLSL_VARYING,4);}
#undef BIND
    return 1;
}
int tg_glsl_uber_vertex_run(tg_glsl_uber_vertex_shader *s,const tg_uber_vertex_input *in,const tg_uber_vertex_uniforms *u,tg_uber_vertex_output *out)
{
    tg_uber_vertex_output r;int i,j,status;
    if(!s||!s->program||!in||!u||!out||!u->sqrt_fn||s->features!=u->features)return TG_FRAGMENT_INVALID;
    s->vm.sqrt_fn=u->sqrt_fn;
    for(i=0;i<3;i++) { s->vm.reg[s->position][i]=in->position[i];s->vm.reg[s->normal][i]=in->normal[i];s->vm.reg[s->light_pos][i]=u->light_position[i];s->vm.reg[s->camera_pos][i]=u->camera_position[i];if(s->features&TG_FRAG_BUMP)s->vm.reg[s->tangent][i]=in->tangent[i]; }
    for(i=0;i<2;i++)s->vm.reg[s->uv][i]=in->uv[i];
    for(i=0;i<4;i++)for(j=0;j<4;j++) { s->vm.reg[s->mvp+i][j]=u->mvp[i*4+j];if(s->features&TG_FRAG_SHADOW)s->vm.reg[s->light_mvp+i][j]=u->light_mvp[i*4+j]; }
    status=tg_glsl_vm_run_from(&s->vm,s->program->code,s->program->instruction_count,s->program->entry_pc);
    if(status!=TG_VM_OK)return status==TG_VM_UNDEFINED?TG_FRAGMENT_UNDEFINED:TG_FRAGMENT_INVALID;
    for(i=0;i<4;i++){r.clip[i]=s->vm.reg[s->program->output_register][i];r.varying.light_clip[i]=(s->features&TG_FRAG_SHADOW)?s->vm.reg[s->out_light_clip][i]:0;}
    for(i=0;i<3;i++){r.varying.light[i]=s->vm.reg[s->out_light][i];r.varying.half_vector[i]=s->vm.reg[s->out_half][i];r.varying.normal[i]=(s->features&TG_FRAG_BUMP)?0:s->vm.reg[s->out_normal][i];}
    for(i=0;i<2;i++)r.varying.uv[i]=s->vm.reg[s->out_uv][i];*out=r;return TG_FRAGMENT_OK;
}
