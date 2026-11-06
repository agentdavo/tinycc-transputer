/* A deliberately small OpenGL ES 2.0 API subset. No EGL or shader support. */
#ifndef DISTFB_GLES2_SUBSET_H
#define DISTFB_GLES2_SUBSET_H
#include "tinygl.h"
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef unsigned char GLboolean;
typedef float GLclampf;
typedef float GLfloat;
typedef int GLint;
typedef int GLsizei;
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505
#define GL_SCISSOR_TEST 0x0C11
#define GL_SCISSOR_BOX 0x0C10
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_DEPTH_WRITEMASK 0x0B72
#define GL_DEPTH_BUFFER_BIT 0x00000100u
#define GL_STENCIL_BUFFER_BIT 0x00000400u
#define GL_COLOR_BUFFER_BIT 0x00004000u
typedef struct {
    tg_context *target;
    unsigned int clear_argb, color_write_mask;
    GLboolean depth_write;
    GLenum error;
    GLint scissor_x, scissor_y;
    GLsizei scissor_width, scissor_height;
    GLboolean scissor_enabled;
    struct tg_gles_draw_state *draw;
    struct tg_gles_program_state *programs;
    struct tg_gles_texture_state *textures;
} tg_gles_state;
/* Explicit embedding hook, not part of OpenGL ES or EGL. One current state
 * per transputer address space; call before using the gl* subset. */
void tgGlesInit(tg_gles_state *state, tg_context *target);
void tgGlesMakeCurrent(tg_gles_state *state);
/* Embedding helpers for the separately implemented vertex-input subset. */
tg_gles_state *tgGlesCurrentState(void);
void tgGlesSetError(GLenum error);
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void glDepthMask(GLboolean flag);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
GLboolean glIsEnabled(GLenum cap);
void glGetBooleanv(GLenum pname, GLboolean *data);
void glGetIntegerv(GLenum pname, GLint *data);
void glGetFloatv(GLenum pname, GLfloat *data);
void glClear(GLbitfield mask);
GLenum glGetError(void);
#endif
