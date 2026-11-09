/* Embedding interface of the port's OpenGL ES 2.0 implementation (gles2.c):
 * what EGL would provide on a real system. */
#ifndef DE_GLES2_PORT_H
#define DE_GLES2_PORT_H
#include "include/OpenGLES/ES2/gl.h"

/* The default framebuffer: RGBA8 colour and float depth, rows bottom first
 * (GL window coordinates). `owned` marks the rows this context draws, by
 * window row (0 = bottom); other rows are neither cleared nor drawn, which is
 * how a node renders its share of the frame. NULL means every row. */
void deGlInit(void);
void deGlSetWindow(int width, int height, unsigned char *color, float *depth, const unsigned char *owned);

typedef struct {
    unsigned long draws, triangles, clipped, culled, vertices;
    unsigned long fragments, depth_rejected, shaded;
} de_gl_stats;
void deGlStats(de_gl_stats *out, int reset);
#endif
