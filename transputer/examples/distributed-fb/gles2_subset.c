#include "gles2_subset.h"
static tg_gles_state *current;
static unsigned int channel(GLclampf x)
{
    if (x < 0.0f) x=0.0f;
    if (x > 1.0f) x=1.0f;
    return (unsigned int)(x*255.0f+0.5f);
}
void tgGlesInit(tg_gles_state *s, tg_context *target)
{
    s->target=target; s->clear_argb=0; s->color_write_mask=0xffffffffu;
    s->depth_write=GL_TRUE; s->error=GL_NO_ERROR;
}
void tgGlesMakeCurrent(tg_gles_state *s) { current=s; }
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    if (current) current->clear_argb=(channel(a)<<24)|(channel(r)<<16)|(channel(g)<<8)|channel(b);
}
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    if (current) current->color_write_mask=(a?0xff000000u:0)|(r?0x00ff0000u:0)|(g?0x0000ff00u:0)|(b?0x000000ffu:0);
}
void glDepthMask(GLboolean flag) { if (current) current->depth_write=flag ? GL_TRUE : GL_FALSE; }
GLenum glGetError(void)
{
    GLenum e;
    if (!current) return GL_NO_ERROR;
    e=current->error; current->error=GL_NO_ERROR; return e;
}
void glClear(GLbitfield mask)
{
    int i; tg_context *c; unsigned int bits;
    if (!current) return;
    if (mask & ~(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT)) {
        if (current->error == GL_NO_ERROR) current->error=GL_INVALID_VALUE;
        return;
    }
    c=current->target;
    bits=current->color_write_mask;
    if ((mask & (GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT)) == (GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT)
        && bits==0xffffffffu && current->depth_write) {
        tg_clear(c,current->clear_argb);
        return;
    }
    for (i=0;i<TG_TILE_W*TG_TILE_H;++i) {
        if (mask & GL_COLOR_BUFFER_BIT)
            c->color[i]=(c->color[i]&~bits)|(current->clear_argb&bits);
        if ((mask & GL_DEPTH_BUFFER_BIT) && current->depth_write)
            c->depth[i]=-2147483647;
    }
}
