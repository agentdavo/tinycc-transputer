/* Bounded ES 2.0-style vertex input and indexed triangle subset. */
#ifndef DISTFB_GLES2_DRAW_H
#define DISTFB_GLES2_DRAW_H
#include <stddef.h>
#include "gles2_subset.h"
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_FIXED 0x140c
#define GL_TRIANGLES 0x0004
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88e0
#define GL_STATIC_DRAW 0x88e4
#define GL_DYNAMIC_DRAW 0x88e8
#define TG_GLES_MAX_ATTRIBUTES 8
#define TG_GLES_MAX_BUFFERS 8
#define TG_GLES_VERTEX_CACHE_SIZE 64
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef struct {
    GLuint name;
    unsigned int size;
    GLenum usage;
    unsigned char reserved, created;
} tg_gles_buffer;
typedef struct {
    const void *pointer;
    GLuint buffer;
    GLint size;
    GLenum type;
    GLsizei stride;
    GLboolean normalized, enabled;
} tg_gles_attribute;
/* This callback is an embedding hook, not GLSL or an OpenGL entry point.
 * It receives expanded attributes and produces the existing rasterizer's
 * projected coordinates. Return zero to discard a vertex. */
typedef int (*tg_gles_vertex_fn)(const GLfloat attributes[TG_GLES_MAX_ATTRIBUTES][4],
                                 tg_screen_vertex *out, void *user);
typedef struct {
    unsigned int index;
    int valid;
    tg_screen_vertex value;
} tg_gles_vertex_cache_entry;
typedef struct tg_gles_draw_state {
    tg_gles_buffer buffers[TG_GLES_MAX_BUFFERS];
    tg_gles_attribute attributes[TG_GLES_MAX_ATTRIBUTES];
    unsigned char *arena;
    unsigned int slot_size;
    GLuint array_buffer, element_buffer;
    tg_gles_vertex_fn vertex;
    void *vertex_user;
    GLboolean persistent_cache;
    tg_gles_vertex_cache_entry cache[TG_GLES_VERTEX_CACHE_SIZE];
} tg_gles_draw_state;
/* The caller owns the arena and state for the lifetime of the GL context.
 * Storage is split into eight equal aligned slots; no malloc is needed. */
void tgGlesInitDraw(tg_gles_state *context, tg_gles_draw_state *state,
                    void *arena, unsigned int arena_size,
                    tg_gles_vertex_fn vertex, void *user);
/* Required after changing callback-owned vertex state outside the GL API. */
void tgGlesInvalidateVertexCache(tg_gles_draw_state *state);
void glGenBuffers(GLsizei n, GLuint *buffers);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
void glBindBuffer(GLenum target, GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void *pointer);
void glEnableVertexAttribArray(GLuint index);
void glDisableVertexAttribArray(GLuint index);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
#endif
