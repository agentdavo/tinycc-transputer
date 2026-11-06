#include "glsl_fragment_shader.h"
void tg_glsl_shader_init(tg_glsl_fragment_shader *s,const tg_glsl_fragment_program *p)
{ int i;s->program=p;tg_glsl_fragment_init(p,&s->vm);for(i=0;i<TG_GLSL_SYMBOLS;i++)s->varying_input[i]=TG_GLSL_INPUT_NONE; }
int tg_glsl_shader_varying(tg_glsl_fragment_shader *s,const char *name,int input)
{
    int i=tg_glsl_find_symbol(s->program,name),w;
    if(i<0 || s->program->symbols[i].kind!=TG_GLSL_VARYING)return 0;
    w=input==TG_GLSL_INPUT_UV?2:input==TG_GLSL_INPUT_LIGHT_CLIP?4:
      (input==TG_GLSL_INPUT_NORMAL||input==TG_GLSL_INPUT_LIGHT||input==TG_GLSL_INPUT_HALF)?3:0;
    if(!w||s->program->symbols[i].width!=w)return 0;
    s->varying_input[i]=input;return 1;
}
int tg_glsl_shader_uniform(tg_glsl_fragment_shader *s,const char *name,const float *value,int width)
{
    int i=tg_glsl_find_symbol(s->program,name),j;
    if(i<0||!value||s->program->symbols[i].kind!=TG_GLSL_UNIFORM||s->program->symbols[i].width!=width)return 0;
    for(j=0;j<width;j++)s->vm.reg[s->program->symbols[i].slot][j]=value[j];return 1;
}
int tg_glsl_shader_texture(tg_glsl_fragment_shader *s,const char *name,const tg_fragment_texture *texture)
{
    int i=tg_glsl_find_symbol(s->program,name);
    if(i<0||s->program->symbols[i].kind!=TG_GLSL_SAMPLER)return 0;
    s->vm.texture[s->program->symbols[i].slot]=texture;return 1;
}
int tg_glsl_shader_fragment(void *user,const tg_fragment_varyings *v,float out[4])
{
    tg_glsl_fragment_shader *s=user;int i,j,status;
    if(!s||!s->program||!v||!out)return TG_FRAGMENT_INVALID;
    for(i=0;i<s->program->symbol_count;i++) if(s->program->symbols[i].kind==TG_GLSL_VARYING) {
        const float *in=0;int w=s->program->symbols[i].width;
        switch(s->varying_input[i]) {
        case TG_GLSL_INPUT_UV:in=v->uv;break; case TG_GLSL_INPUT_NORMAL:in=v->normal;break;
        case TG_GLSL_INPUT_LIGHT:in=v->light;break;case TG_GLSL_INPUT_HALF:in=v->half_vector;break;
        case TG_GLSL_INPUT_LIGHT_CLIP:in=v->light_clip;break;default:return TG_FRAGMENT_INVALID;
        }
        for(j=0;j<w;j++)s->vm.reg[s->program->symbols[i].slot][j]=in[j];
    }
    status=tg_glsl_vm_run(&s->vm,s->program->code,s->program->instruction_count);
    if(status!=TG_VM_OK)return status==TG_VM_UNDEFINED?TG_FRAGMENT_UNDEFINED:TG_FRAGMENT_INVALID;
    for(i=0;i<4;i++)out[i]=s->vm.reg[s->program->output_register][i];return TG_FRAGMENT_OK;
}
