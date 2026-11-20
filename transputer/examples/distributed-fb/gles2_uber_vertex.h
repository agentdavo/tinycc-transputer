/* Native implementation of the operations in dEngine's v_uber.glsl.
 * This is an explicit backend entry point, not GLSL source compilation. */
#ifndef TG_GLES2_UBER_VERTEX_H
#define TG_GLES2_UBER_VERTEX_H
#include "gles2_fragment.h"
typedef struct {
    float position[3], normal[3], tangent[3], uv[2];
} tg_uber_vertex_input;
typedef struct {
    float mvp[16], light_mvp[16], light_position[3], camera_position[3];
    unsigned int features;
    float (*sqrt_fn)(float);
} tg_uber_vertex_uniforms;
typedef struct { float clip[4]; tg_fragment_varyings varying; } tg_uber_vertex_output;
/* Zero-length normalize is undefined in GLSL: reports UNDEFINED and leaves
 * output unchanged. Matrix layout is column-major as in OpenGL ES. */
int tg_uber_vertex(const tg_uber_vertex_input *,const tg_uber_vertex_uniforms *,tg_uber_vertex_output *);
#endif
