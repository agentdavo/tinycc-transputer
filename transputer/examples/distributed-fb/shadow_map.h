/* Nearest depth map in dEngine's RGBA-packed color texture format. */
#ifndef TG_SHADOW_MAP_H
#define TG_SHADOW_MAP_H
typedef struct { int x,y; float depth; } tg_shadow_vertex;
typedef struct { unsigned int width,height; float *depth; unsigned char *rgba; } tg_shadow_map;
int tg_shadow_clear(tg_shadow_map *);
void tg_shadow_triangle(tg_shadow_map *,const tg_shadow_vertex *,const tg_shadow_vertex *,const tg_shadow_vertex *);
#endif
