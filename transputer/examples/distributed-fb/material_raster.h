/* Explicit material raster path; this is not a general GLSL pipeline. */
#ifndef TG_MATERIAL_RASTER_H
#define TG_MATERIAL_RASTER_H
#include "tinygl.h"
#include "gles2_fragment.h"
typedef struct {
    int x, y, depth;
    float inverse_w;
    tg_fragment_varyings varying;
} tg_material_vertex;
typedef int (*tg_material_fragment_fn)(void *,const tg_fragment_varyings *,float out[4]);
typedef struct { int cull,depth_test,depth_write,blend;unsigned int color_mask; } tg_material_draw_options;
typedef struct { float clip[4];tg_fragment_varyings varying; } tg_material_clip_vertex;
/* Return the number of fragments rejected by the material evaluator. */
int tg_material_triangle(tg_context *, const tg_material_vertex *,
                         const tg_material_vertex *, const tg_material_vertex *,
                         const tg_fragment_material *);
/* Straight-alpha source-over, depth-tested without depth writes. */
int tg_material_triangle_blended(tg_context *, const tg_material_vertex *,
                                 const tg_material_vertex *, const tg_material_vertex *,
                                 const tg_fragment_material *);
/* Caller-provided fragment program; return conventions match TG_FRAGMENT_*. */
int tg_material_triangle_custom(tg_context *,const tg_material_vertex *,const tg_material_vertex *,
                                const tg_material_vertex *,tg_material_fragment_fn,void *);
/* cull: 0 neither, 1 back, 2 front, 3 both; blend: 0 replace, 1 src-alpha. */
int tg_material_triangle_config(tg_context *,const tg_material_vertex *,const tg_material_vertex *,
                                const tg_material_vertex *,tg_material_fragment_fn,void *,const tg_material_draw_options *);
int tg_material_clip_triangle(tg_context *,const tg_material_clip_vertex *,const tg_material_clip_vertex *,
                              const tg_material_clip_vertex *,tg_material_fragment_fn,void *,const tg_material_draw_options *,
                              int viewport_x,int viewport_y,int viewport_width,int viewport_height,int logical_height);
#endif
