/* Small fixed-function, GLES-inspired subset; this is not the GLES API. */
#ifndef DISTFB_TINYGL_H
#define DISTFB_TINYGL_H
#define TG_TILE_W 80
#define TG_TILE_H 60
typedef struct { int x, y, z, u, v; } tg_vertex;
/* Projected z is reciprocal camera depth; u and v are divided by depth. */
typedef struct { int x, y, z, u, v; } tg_screen_vertex;
typedef struct {
    unsigned int *color;
    int *depth;
    const unsigned int *texture; /* 8 x 8, row-major ARGB */
    int tile_x, tile_y;
    int matrix[9]; /* Q8 rotation */
    int camera_distance, focal_length; /* Q8 world units, pixels */
    int clip_enabled, clip_x0, clip_y0, clip_x1, clip_y1; /* top-left, exclusive end */
#ifdef TG_GLES_FRAMEBUFFER
    int surface_width,surface_height,surface_pitch;
#endif
} tg_context;
void tg_clear(tg_context *ctx, unsigned int color);
void tg_clear_band(tg_context *ctx, unsigned int color, int row0, int row1);
void tg_transform(const tg_context *ctx, const tg_vertex *src, tg_screen_vertex *dst, int n);
void tg_draw_elements(tg_context *ctx, const tg_screen_vertex *v, const unsigned char *indices, int triangles);
void tg_draw_triangle(tg_context *ctx, const tg_screen_vertex *a, const tg_screen_vertex *b, const tg_screen_vertex *c);
void cube_render(unsigned int *tile, int *depth, int node, unsigned int frame);
void cube_render_band(unsigned int *tile, int *depth, int quadrant, unsigned int frame, int row0, int row1);
unsigned int tg_checksum(const unsigned int *pixels, int n);
#endif
