#ifndef TG_GLSL_UBER_VERTEX_SHADER_H
#define TG_GLSL_UBER_VERTEX_SHADER_H
#include "glsl_fragment_compiler.h"
#include "gles2_uber_vertex.h"
/* Binds the named dEngine vertex interface to a source-compiled program. */
typedef struct {
    const tg_glsl_fragment_program *program;tg_vm_state vm;
    unsigned char position,normal,tangent,uv,mvp,light_mvp,light_pos,camera_pos;
    unsigned char out_light,out_half,out_uv,out_normal,out_light_clip;
    unsigned int features;int uniforms_ready;
} tg_glsl_uber_vertex_shader;
int tg_glsl_uber_vertex_init(tg_glsl_uber_vertex_shader *,const tg_glsl_fragment_program *,unsigned int features);
int tg_glsl_uber_vertex_run(tg_glsl_uber_vertex_shader *,const tg_uber_vertex_input *,const tg_uber_vertex_uniforms *,tg_uber_vertex_output *);
int tg_glsl_uber_vertex_uniforms(tg_glsl_uber_vertex_shader *,const tg_uber_vertex_uniforms *);
int tg_glsl_uber_vertex_run_prepared(tg_glsl_uber_vertex_shader *,const tg_uber_vertex_input *,tg_uber_vertex_output *);
#endif
