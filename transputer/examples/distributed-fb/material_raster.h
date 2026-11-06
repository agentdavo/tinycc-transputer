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
/* Return the number of fragments rejected by the material evaluator. */
int tg_material_triangle(tg_context *, const tg_material_vertex *,
                         const tg_material_vertex *, const tg_material_vertex *,
                         const tg_fragment_material *);
/* Straight-alpha source-over, depth-tested without depth writes. */
int tg_material_triangle_blended(tg_context *, const tg_material_vertex *,
                                 const tg_material_vertex *, const tg_material_vertex *,
                                 const tg_fragment_material *);
#endif
