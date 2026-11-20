#ifndef TG_GLSL_FRAGMENT_SHADER_H
#define TG_GLSL_FRAGMENT_SHADER_H
#include "glsl_fragment_compiler.h"
enum { TG_GLSL_INPUT_NONE=0,TG_GLSL_INPUT_UV,TG_GLSL_INPUT_NORMAL,TG_GLSL_INPUT_LIGHT,TG_GLSL_INPUT_HALF,TG_GLSL_INPUT_LIGHT_CLIP };
typedef struct {
    const tg_glsl_fragment_program *program;
    tg_vm_state vm;
    unsigned char varying_input[TG_GLSL_SYMBOLS];
} tg_glsl_fragment_shader;
void tg_glsl_shader_init(tg_glsl_fragment_shader *,const tg_glsl_fragment_program *);
int tg_glsl_shader_varying(tg_glsl_fragment_shader *,const char *name,int input);
int tg_glsl_shader_uniform(tg_glsl_fragment_shader *,const char *name,const float *value,int width);
int tg_glsl_shader_texture(tg_glsl_fragment_shader *,const char *name,const tg_fragment_texture *);
int tg_glsl_shader_fragment(void *,const tg_fragment_varyings *,float out[4]);
#endif
