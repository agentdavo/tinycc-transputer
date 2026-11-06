/* A deliberately small OpenGL ES 2.0 API subset. No EGL or shader support. */
#ifndef DISTFB_GLES2_SUBSET_H
#define DISTFB_GLES2_SUBSET_H
#include "tinygl.h"
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef float GLclampf;
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_INVALID_VALUE 0x0501
#define GL_DEPTH_BUFFER_BIT 0x00000100u
#define GL_STENCIL_BUFFER_BIT 0x00000400u
#define GL_COLOR_BUFFER_BIT 0x00004000u
typedef struct {
    tg_context *target;
    unsigned int clear_argb, color_write_mask;
    GLboolean depth_write;
    GLenum error;
} tg_gles_state;
/* Explicit embedding hook, not part of OpenGL ES or EGL. One current state
 * per transputer address space; call before using the gl* subset. */
void tgGlesInit(tg_gles_state *state, tg_context *target);
void tgGlesMakeCurrent(tg_gles_state *state);
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void glDepthMask(GLboolean flag);
void glClear(GLbitfield mask);
GLenum glGetError(void);
#endif
