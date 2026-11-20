/* Bounded RGBA8, level-zero, nearest-only texture object subset. */
#ifndef TG_GLES2_TEXTURE_H
#define TG_GLES2_TEXTURE_H
#include "gles2_subset.h"
#include "gles2_fragment.h"
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401
#endif
#define GL_TEXTURE_2D 0x0de1
#define GL_TEXTURE0 0x84c0
#define GL_RGBA 0x1908
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812f
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define TG_GLES_TEXTURES 8
#define TG_GLES_TEXTURE_UNITS 4
typedef struct { GLuint name; unsigned int size; tg_fragment_texture view; unsigned char reserved,created; } tg_gles_texture;
typedef struct tg_gles_texture_state {
    tg_gles_texture objects[TG_GLES_TEXTURES];
    GLuint bound[TG_GLES_TEXTURE_UNITS],next_name;
    unsigned int active,slot_size;
    unsigned char *arena;
} tg_gles_texture_state;
void tgGlesInitTextures(tg_gles_state *,tg_gles_texture_state *,void *arena,unsigned int bytes);
/* Returns null for an unbound or undefined texture. */
const tg_fragment_texture *tgGlesTextureUnit(unsigned int unit);
tg_gles_texture *tgGlesTextureObject(GLuint name);
void glGenTextures(GLsizei,GLuint *);
void glDeleteTextures(GLsizei,const GLuint *);
void glActiveTexture(GLenum);
void glBindTexture(GLenum,GLuint);
void glTexParameteri(GLenum,GLenum,GLint);
void glTexParameterf(GLenum,GLenum,GLfloat);
void glTexImage2D(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void *);
#endif
