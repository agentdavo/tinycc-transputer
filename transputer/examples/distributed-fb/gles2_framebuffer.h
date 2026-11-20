/* Caller-owned, bounded color-texture + depth-renderbuffer framebuffer path.
 * Opt-in: compile all participating raster files with TG_GLES_FRAMEBUFFER. */
#ifndef TG_GLES2_FRAMEBUFFER_H
#define TG_GLES2_FRAMEBUFFER_H
#include "gles2_texture.h"
#define GL_FRAMEBUFFER 0x8d40
#define GL_RENDERBUFFER 0x8d41
#define GL_COLOR_ATTACHMENT0 0x8ce0
#define GL_DEPTH_ATTACHMENT 0x8d00
#define GL_DEPTH_COMPONENT16 0x81a5
#define GL_FRAMEBUFFER_COMPLETE 0x8cd5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8cd6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8cd7
#define TG_GLES_FB_COUNT 4
typedef struct { GLuint name,color,depth;unsigned char reserved,created; } tg_gles_fb_object;
typedef struct { GLuint name;unsigned int width,height;unsigned char reserved,created; } tg_gles_rb_object;
typedef struct tg_gles_framebuffer_state {
    tg_gles_fb_object fb[TG_GLES_FB_COUNT];tg_gles_rb_object rb[TG_GLES_FB_COUNT];
    GLuint current,renderbuffer,next_fb,next_rb;
    unsigned int *color_scratch;unsigned int color_words;int *depth_arena;unsigned int depth_slot_words;
    tg_context target,*default_target;int default_width,default_height;unsigned char loaded;
} tg_gles_framebuffer_state;
void tgGlesInitFramebuffers(tg_gles_state *,tg_gles_framebuffer_state *,unsigned int *color_scratch,unsigned int color_words,int *depth_arena,unsigned int depth_words);
int tgGlesFramebufferReady(tg_gles_state *);
int tgGlesFramebufferTextureAttached(tg_gles_state *,GLuint);
void tgGlesFramebufferDetachTexture(tg_gles_state *,GLuint);
void glGenFramebuffers(GLsizei,GLuint *);void glDeleteFramebuffers(GLsizei,const GLuint *);
void glBindFramebuffer(GLenum,GLuint);GLenum glCheckFramebufferStatus(GLenum);
void glFramebufferTexture2D(GLenum,GLenum,GLenum,GLuint,GLint);
void glGenRenderbuffers(GLsizei,GLuint *);void glDeleteRenderbuffers(GLsizei,const GLuint *);
void glBindRenderbuffer(GLenum,GLuint);void glRenderbufferStorage(GLenum,GLenum,GLsizei,GLsizei);
void glFramebufferRenderbuffer(GLenum,GLenum,GLenum,GLuint);
void glReadPixels(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void *);
#endif
