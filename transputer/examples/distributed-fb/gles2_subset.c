#include "gles2_subset.h"
#ifdef TG_GLES_FRAMEBUFFER
#include "gles2_framebuffer.h"
#define TARGET_WIDTH(c) ((c)->surface_width)
#define TARGET_HEIGHT(c) ((c)->surface_height)
#define TARGET_PITCH(c) ((c)->surface_pitch)
#else
#define TARGET_WIDTH(c) TG_TILE_W
#define TARGET_HEIGHT(c) TG_TILE_H
#define TARGET_PITCH(c) TG_TILE_W
#endif
static tg_gles_state *current;
static void update_clip(void);
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
    s->scissor_x=0; s->scissor_y=0; s->scissor_width=160; s->scissor_height=120;
    s->scissor_enabled=GL_FALSE; s->draw=0; s->programs=0; s->textures=0; target->clip_enabled=0;
#ifdef TG_GLES_FRAMEBUFFER
    s->framebuffers=0;s->logical_width=160;s->logical_height=120;
    target->surface_width=TG_TILE_W;target->surface_height=TG_TILE_H;target->surface_pitch=TG_TILE_W;
#endif
#ifdef TG_GLES_VM_PROGRAM
    s->viewport_x=s->viewport_y=0;s->viewport_width=160;s->viewport_height=120;
    s->depth_enabled=s->cull_enabled=s->blend_enabled=GL_FALSE;s->cull_face=GL_BACK;s->blend_src=GL_ONE;s->blend_dst=GL_ZERO;
#endif
}
void tgGlesMakeCurrent(tg_gles_state *s) { current=s; if (s) update_clip(); }
tg_gles_state *tgGlesCurrentState(void) { return current; }
void tgGlesSetError(GLenum error) { if (current && current->error==GL_NO_ERROR) current->error=error; }
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    if (current) current->clear_argb=(channel(a)<<24)|(channel(r)<<16)|(channel(g)<<8)|channel(b);
}
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    if (current) current->color_write_mask=(a?0xff000000u:0)|(r?0x00ff0000u:0)|(g?0x0000ff00u:0)|(b?0x000000ffu:0);
}
void glDepthMask(GLboolean flag) { if (current) current->depth_write=flag ? GL_TRUE : GL_FALSE; }
static void update_clip(void)
{
    long long x0=current->scissor_x, y0=current->scissor_y;
    long long x1=x0+current->scissor_width, y1=y0+current->scissor_height;
    tg_context *c=current->target;
#ifdef TG_GLES_FRAMEBUFFER
    int logical_width=current->logical_width,logical_height=current->logical_height;
#else
    int logical_width=160,logical_height=120;
#endif
    if (x0<0) x0=0;
    if (x0>logical_width) x0=logical_width;
    if (x1<0) x1=0;
    if (x1>logical_width) x1=logical_width;
    if (y0<0) y0=0;
    if (y0>logical_height) y0=logical_height;
    if (y1<0) y1=0;
    if (y1>logical_height) y1=logical_height;
    c->clip_x0=(int)x0; c->clip_x1=(int)x1;
    c->clip_y0=logical_height-(int)y1; c->clip_y1=logical_height-(int)y0;
    c->clip_enabled=current->scissor_enabled;
}
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (!current) return;
    if (width<0 || height<0) {
        if (current->error==GL_NO_ERROR) current->error=GL_INVALID_VALUE;
        return;
    }
    current->scissor_x=x; current->scissor_y=y;
    current->scissor_width=width; current->scissor_height=height;
    update_clip();
}
void glEnable(GLenum cap)
{
    if (!current) return;
#ifdef TG_GLES_VM_PROGRAM
    if(cap==GL_DEPTH_TEST){current->depth_enabled=GL_TRUE;return;}
    if(cap==GL_CULL_FACE){current->cull_enabled=GL_TRUE;return;}
    if(cap==GL_BLEND){current->blend_enabled=GL_TRUE;return;}
#endif
    if (cap!=GL_SCISSOR_TEST) { if (current->error==GL_NO_ERROR) current->error=GL_INVALID_ENUM; return; }
    current->scissor_enabled=GL_TRUE; update_clip();
}
void glDisable(GLenum cap)
{
    if (!current) return;
#ifdef TG_GLES_VM_PROGRAM
    if(cap==GL_DEPTH_TEST){current->depth_enabled=GL_FALSE;return;}
    if(cap==GL_CULL_FACE){current->cull_enabled=GL_FALSE;return;}
    if(cap==GL_BLEND){current->blend_enabled=GL_FALSE;return;}
#endif
    if (cap!=GL_SCISSOR_TEST) { if (current->error==GL_NO_ERROR) current->error=GL_INVALID_ENUM; return; }
    current->scissor_enabled=GL_FALSE; update_clip();
}
GLboolean glIsEnabled(GLenum cap)
{
    if (!current) return GL_FALSE;
#ifdef TG_GLES_VM_PROGRAM
    if(cap==GL_DEPTH_TEST)return current->depth_enabled;
    if(cap==GL_CULL_FACE)return current->cull_enabled;
    if(cap==GL_BLEND)return current->blend_enabled;
#endif
    if (cap!=GL_SCISSOR_TEST) { if (current->error==GL_NO_ERROR) current->error=GL_INVALID_ENUM; return GL_FALSE; }
    return current->scissor_enabled;
}
/* Shared query path for the implemented integer and boolean state. All three
 * ES query types accept these pnames and apply their corresponding conversion. */
static int get_state(GLenum pname, GLint v[4])
{
    if (!current) return 0;
    switch (pname) {
    case GL_SCISSOR_BOX:
        v[0]=current->scissor_x; v[1]=current->scissor_y;
        v[2]=current->scissor_width; v[3]=current->scissor_height;
        return 4;
#ifdef TG_GLES_VM_PROGRAM
    case GL_VIEWPORT:v[0]=current->viewport_x;v[1]=current->viewport_y;v[2]=current->viewport_width;v[3]=current->viewport_height;return 4;
    case GL_DEPTH_TEST:v[0]=current->depth_enabled;return 1;
    case GL_CULL_FACE:v[0]=current->cull_enabled;return 1;
    case GL_BLEND:v[0]=current->blend_enabled;return 1;
#endif
    case GL_SCISSOR_TEST:
        v[0]=current->scissor_enabled; return 1;
    case GL_DEPTH_WRITEMASK:
        v[0]=current->depth_write; return 1;
    case GL_COLOR_WRITEMASK:
        v[0]=(current->color_write_mask & 0x00ff0000u)!=0;
        v[1]=(current->color_write_mask & 0x0000ff00u)!=0;
        v[2]=(current->color_write_mask & 0x000000ffu)!=0;
        v[3]=(current->color_write_mask & 0xff000000u)!=0;
        return 4;
    default:
        if (current->error==GL_NO_ERROR) current->error=GL_INVALID_ENUM;
        return 0;
    }
}
#ifdef TG_GLES_VM_PROGRAM
void glViewport(GLint x,GLint y,GLsizei width,GLsizei height)
{
    if(!current)return;if(width<0||height<0){tgGlesSetError(GL_INVALID_VALUE);return;}
    current->viewport_x=x;current->viewport_y=y;current->viewport_width=width;current->viewport_height=height;
}
void glCullFace(GLenum mode)
{
    if(!current)return;if(mode!=GL_FRONT&&mode!=GL_BACK&&mode!=GL_FRONT_AND_BACK){tgGlesSetError(GL_INVALID_ENUM);return;}current->cull_face=mode;
}
void glBlendFunc(GLenum source,GLenum destination)
{
    if(!current)return;
    if(!((source==GL_ONE&&destination==GL_ZERO)||(source==GL_SRC_ALPHA&&destination==GL_ONE_MINUS_SRC_ALPHA))){tgGlesSetError(GL_INVALID_ENUM);return;}
    current->blend_src=source;current->blend_dst=destination;
}
#endif
void glGetBooleanv(GLenum pname, GLboolean *data)
{
    GLint v[4]; int i, n=get_state(pname,v);
    for (i=0;i<n;i++) data[i]=v[i] ? GL_TRUE : GL_FALSE;
}
void glGetIntegerv(GLenum pname, GLint *data)
{
    GLint v[4]; int i, n=get_state(pname,v);
    for (i=0;i<n;i++) data[i]=v[i];
}
void glGetFloatv(GLenum pname, GLfloat *data)
{
    GLint v[4]; int i, n=get_state(pname,v);
    for (i=0;i<n;i++) data[i]=(GLfloat)v[i];
}
GLenum glGetError(void)
{
    GLenum e;
    if (!current) return GL_NO_ERROR;
    e=current->error; current->error=GL_NO_ERROR; return e;
}
void glClear(GLbitfield mask)
{
    int i,x,y; tg_context *c; unsigned int bits;
    if (!current) return;
#ifdef TG_GLES_FRAMEBUFFER
    if(current->framebuffers && !tgGlesFramebufferReady(current)) { tgGlesSetError(GL_INVALID_FRAMEBUFFER_OPERATION);return; }
#endif
    if (mask & ~(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT)) {
        if (current->error == GL_NO_ERROR) current->error=GL_INVALID_VALUE;
        return;
    }
    c=current->target;
    bits=current->color_write_mask;
    if ((mask & (GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT)) == (GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT)
        && bits==0xffffffffu && current->depth_write) {
        if (!c->clip_enabled) { tg_clear(c,current->clear_argb); return; }
        if (c->clip_x0<=c->tile_x && c->clip_x1>=c->tile_x+TARGET_WIDTH(c)) {
            tg_clear_band(c,current->clear_argb,c->clip_y0-c->tile_y,c->clip_y1-c->tile_y);
            return;
        }
    }
    for (i=0;i<TARGET_WIDTH(c)*TARGET_HEIGHT(c);++i) {
        int offset=(i/TARGET_WIDTH(c))*TARGET_PITCH(c)+(i%TARGET_WIDTH(c));
        x=i%TARGET_WIDTH(c)+c->tile_x; y=i/TARGET_WIDTH(c)+c->tile_y;
        if (c->clip_enabled && (x<c->clip_x0 || x>=c->clip_x1 || y<c->clip_y0 || y>=c->clip_y1)) continue;
        if (mask & GL_COLOR_BUFFER_BIT)
            c->color[offset]=(c->color[offset]&~bits)|(current->clear_argb&bits);
        if ((mask & GL_DEPTH_BUFFER_BIT) && current->depth_write)
            c->depth[offset]=-2147483647;
    }
}
