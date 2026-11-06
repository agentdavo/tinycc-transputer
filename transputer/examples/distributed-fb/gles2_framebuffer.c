#include "gles2_framebuffer.h"
static tg_gles_framebuffer_state *state(void)
{ tg_gles_state *c=tgGlesCurrentState();if(!c)return 0;if(!c->framebuffers)tgGlesSetError(GL_INVALID_OPERATION);return c->framebuffers; }
static int find_fb(tg_gles_framebuffer_state *s,GLuint n)
{ int i;for(i=0;i<TG_GLES_FB_COUNT;i++)if(s->fb[i].reserved&&s->fb[i].name==n)return i;return -1; }
static int find_rb(tg_gles_framebuffer_state *s,GLuint n)
{ int i;for(i=0;i<TG_GLES_FB_COUNT;i++)if(s->rb[i].reserved&&s->rb[i].name==n)return i;return -1; }
static GLenum status(tg_gles_state *c)
{
    tg_gles_framebuffer_state *s=c->framebuffers;tg_gles_texture *t;tg_gles_rb_object *r;int f,b;
    if(!s||!s->current)return GL_FRAMEBUFFER_COMPLETE;
    f=find_fb(s,s->current);if(f<0)return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    if(!s->fb[f].color||!s->fb[f].depth)return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    if(!c->textures || !(t=tgGlesTextureObject(s->fb[f].color)) || !t->view.rgba || (b=find_rb(s,s->fb[f].depth))<0)return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    r=&s->rb[b];if(!r->created || !r->width || !r->height || r->width!=t->view.width || r->height!=t->view.height ||
       !s->color_scratch || !s->depth_arena || r->height>s->color_words/r->width || r->height>s->depth_slot_words/r->width)return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    return GL_FRAMEBUFFER_COMPLETE;
}
static void flush(tg_gles_state *c)
{
    tg_gles_framebuffer_state *s=c->framebuffers;tg_gles_texture *t;unsigned int x,y,w,h;
    if(!s || !s->current || !s->loaded || status(c)!=GL_FRAMEBUFFER_COMPLETE)return;
    t=tgGlesTextureObject(s->fb[find_fb(s,s->current)].color);w=t->view.width;h=t->view.height;
    for(y=0;y<h;y++)for(x=0;x<w;x++) {
        unsigned int color=s->color_scratch[y*w+x],i=((h-1-y)*w+x)*4;unsigned char *dst=(unsigned char *)t->view.rgba+i;
        dst[0]=(unsigned char)(color>>16);dst[1]=(unsigned char)(color>>8);dst[2]=(unsigned char)color;dst[3]=(unsigned char)(color>>24);
    }
    s->loaded=0;
}
static void select_target(tg_gles_state *c)
{
    tg_gles_framebuffer_state *s=c->framebuffers;tg_gles_texture *t;unsigned int x,y,w,h;int f,r;
    if(!s->current){c->target=s->default_target;c->logical_width=s->default_width;c->logical_height=s->default_height;tgGlesMakeCurrent(c);return;}
    if(status(c)!=GL_FRAMEBUFFER_COMPLETE)return;
    f=find_fb(s,s->current);r=find_rb(s,s->fb[f].depth);t=tgGlesTextureObject(s->fb[f].color);w=t->view.width;h=t->view.height;
    for(y=0;y<h;y++)for(x=0;x<w;x++) {
        unsigned int i=((h-1-y)*w+x)*4;const unsigned char *p=t->view.rgba+i;
        s->color_scratch[y*w+x]=((unsigned int)p[3]<<24)|((unsigned int)p[0]<<16)|((unsigned int)p[1]<<8)|p[2];
    }
    s->target.color=s->color_scratch;s->target.depth=s->depth_arena+(unsigned int)r*s->depth_slot_words;
    s->target.texture=0;s->target.tile_x=s->target.tile_y=0;s->target.surface_width=w;s->target.surface_height=h;s->target.surface_pitch=w;
    c->target=&s->target;c->logical_width=w;c->logical_height=h;s->loaded=1;tgGlesMakeCurrent(c);
}
void tgGlesInitFramebuffers(tg_gles_state *c,tg_gles_framebuffer_state *s,unsigned int *color,unsigned int color_words,int *depth,unsigned int depth_words)
{
    int i;s->current=s->renderbuffer=0;s->next_fb=s->next_rb=1;s->loaded=0;s->color_scratch=color;s->color_words=color_words;
    s->depth_arena=depth;s->depth_slot_words=depth_words/TG_GLES_FB_COUNT;s->default_target=c->target;s->default_width=c->logical_width;s->default_height=c->logical_height;
    for(i=0;i<TG_GLES_FB_COUNT;i++){s->fb[i].reserved=s->fb[i].created=0;s->rb[i].reserved=s->rb[i].created=0;}
    c->framebuffers=s;
}
int tgGlesFramebufferReady(tg_gles_state *c)
{ return !c->framebuffers || status(c)==GL_FRAMEBUFFER_COMPLETE; }
int tgGlesFramebufferTextureAttached(tg_gles_state *c,GLuint name)
{ int i;if(!c||!c->framebuffers||!name)return 0;for(i=0;i<TG_GLES_FB_COUNT;i++)if(c->framebuffers->fb[i].reserved&&c->framebuffers->fb[i].color==name)return 1;return 0; }
void tgGlesFramebufferDetachTexture(tg_gles_state *c,GLuint name)
{
    int i;tg_gles_framebuffer_state *s;if(!c||!(s=c->framebuffers))return;flush(c);
    for(i=0;i<TG_GLES_FB_COUNT;i++)if(s->fb[i].reserved&&s->fb[i].color==name)s->fb[i].color=0;
    if(s->current)select_target(c);
}
void glGenFramebuffers(GLsizei n,GLuint *names)
{
    tg_gles_framebuffer_state *s=state();int i,j,count=0;if(!s)return;if(n<0||(n&&!names)){tgGlesSetError(GL_INVALID_VALUE);return;}
    for(i=0;i<TG_GLES_FB_COUNT;i++)if(!s->fb[i].reserved)count++;if(n>count){tgGlesSetError(GL_OUT_OF_MEMORY);return;}
    for(i=0;i<n;i++){for(j=0;j<TG_GLES_FB_COUNT;j++)if(!s->fb[j].reserved)break;while(!s->next_fb||find_fb(s,s->next_fb)>=0)s->next_fb++;
        s->fb[j].name=names[i]=s->next_fb++;s->fb[j].reserved=1;s->fb[j].created=0;s->fb[j].color=s->fb[j].depth=0;}
}
void glBindFramebuffer(GLenum target,GLuint name)
{
    tg_gles_framebuffer_state *s=state();tg_gles_state *c=tgGlesCurrentState();int i;if(!s)return;
    if(target!=GL_FRAMEBUFFER){tgGlesSetError(GL_INVALID_ENUM);return;}i=name?find_fb(s,name):-1;
    if(name&&i<0){tgGlesSetError(GL_INVALID_OPERATION);return;}if(s->current==name)return;
    flush(c);s->current=name;s->loaded=0;if(name)s->fb[i].created=1;select_target(c);
}
GLenum glCheckFramebufferStatus(GLenum target)
{ tg_gles_framebuffer_state *s=state();if(!s)return 0;if(target!=GL_FRAMEBUFFER){tgGlesSetError(GL_INVALID_ENUM);return 0;}return status(tgGlesCurrentState()); }
void glDeleteFramebuffers(GLsizei n,const GLuint *names)
{
    tg_gles_framebuffer_state *s=state();int i,j;if(!s)return;if(n<0||(n&&!names)){tgGlesSetError(GL_INVALID_VALUE);return;}
    for(i=0;i<n;i++)if((j=find_fb(s,names[i]))>=0){if(s->current==names[i])glBindFramebuffer(GL_FRAMEBUFFER,0);s->fb[j].reserved=s->fb[j].created=0;}
}
void glFramebufferTexture2D(GLenum target,GLenum attachment,GLenum textarget,GLuint texture,GLint level)
{
    tg_gles_framebuffer_state *s=state();tg_gles_state *c=tgGlesCurrentState();int f;if(!s)return;
    if(target!=GL_FRAMEBUFFER||attachment!=GL_COLOR_ATTACHMENT0||textarget!=GL_TEXTURE_2D){tgGlesSetError(GL_INVALID_ENUM);return;}
    if(level!=0){tgGlesSetError(GL_INVALID_VALUE);return;}if(!s->current||(f=find_fb(s,s->current))<0 || (texture&&!tgGlesTextureObject(texture))){tgGlesSetError(GL_INVALID_OPERATION);return;}
    flush(c);s->fb[f].color=texture;s->loaded=0;select_target(c);
}
void glGenRenderbuffers(GLsizei n,GLuint *names)
{
    tg_gles_framebuffer_state *s=state();int i,j,count=0;if(!s)return;if(n<0||(n&&!names)){tgGlesSetError(GL_INVALID_VALUE);return;}
    for(i=0;i<TG_GLES_FB_COUNT;i++)if(!s->rb[i].reserved)count++;if(n>count){tgGlesSetError(GL_OUT_OF_MEMORY);return;}
    for(i=0;i<n;i++){for(j=0;j<TG_GLES_FB_COUNT;j++)if(!s->rb[j].reserved)break;while(!s->next_rb||find_rb(s,s->next_rb)>=0)s->next_rb++;
        s->rb[j].name=names[i]=s->next_rb++;s->rb[j].reserved=1;s->rb[j].created=0;s->rb[j].width=s->rb[j].height=0;}
}
void glBindRenderbuffer(GLenum target,GLuint name)
{
    tg_gles_framebuffer_state *s=state();int i;if(!s)return;if(target!=GL_RENDERBUFFER){tgGlesSetError(GL_INVALID_ENUM);return;}
    i=name?find_rb(s,name):-1;if(name&&i<0){tgGlesSetError(GL_INVALID_OPERATION);return;}s->renderbuffer=name;if(name)s->rb[i].created=1;
}
void glRenderbufferStorage(GLenum target,GLenum format,GLsizei width,GLsizei height)
{
    tg_gles_framebuffer_state *s=state();tg_gles_state *c=tgGlesCurrentState();unsigned int i,n;int r;if(!s)return;
    if(target!=GL_RENDERBUFFER||format!=GL_DEPTH_COMPONENT16){tgGlesSetError(GL_INVALID_ENUM);return;}
    if(width<0||height<0||width>32767||height>32767){tgGlesSetError(GL_INVALID_VALUE);return;}
    if((r=find_rb(s,s->renderbuffer))<0){tgGlesSetError(GL_INVALID_OPERATION);return;}
    if(width && (unsigned int)height>s->depth_slot_words/(unsigned int)width){tgGlesSetError(GL_OUT_OF_MEMORY);return;}
    n=(unsigned int)width*(unsigned int)height;if(n&&!s->depth_arena){tgGlesSetError(GL_OUT_OF_MEMORY);return;}
    flush(c);s->rb[r].width=width;s->rb[r].height=height;
    for(i=0;i<n;i++)s->depth_arena[(unsigned int)r*s->depth_slot_words+i]=-2147483647;
    if(s->current)select_target(c);
}
void glFramebufferRenderbuffer(GLenum target,GLenum attachment,GLenum renderbuffertarget,GLuint rb)
{
    tg_gles_framebuffer_state *s=state();tg_gles_state *c=tgGlesCurrentState();int f,r;if(!s)return;
    if(target!=GL_FRAMEBUFFER||attachment!=GL_DEPTH_ATTACHMENT||renderbuffertarget!=GL_RENDERBUFFER){tgGlesSetError(GL_INVALID_ENUM);return;}
    if(!s->current||(f=find_fb(s,s->current))<0 || (rb&&((r=find_rb(s,rb))<0||!s->rb[r].created))){tgGlesSetError(GL_INVALID_OPERATION);return;}
    flush(c);s->fb[f].depth=rb;s->loaded=0;select_target(c);
}
void glDeleteRenderbuffers(GLsizei n,const GLuint *names)
{
    tg_gles_framebuffer_state *s=state();tg_gles_state *c=tgGlesCurrentState();int i,j,k;if(!s)return;if(n<0||(n&&!names)){tgGlesSetError(GL_INVALID_VALUE);return;}
    for(i=0;i<n;i++)if((j=find_rb(s,names[i]))>=0){flush(c);for(k=0;k<TG_GLES_FB_COUNT;k++)if(s->fb[k].reserved&&s->fb[k].depth==names[i])s->fb[k].depth=0;
        if(s->renderbuffer==names[i])s->renderbuffer=0;s->rb[j].reserved=s->rb[j].created=0;if(s->current)select_target(c);}
}
void glReadPixels(GLint x,GLint y,GLsizei width,GLsizei height,GLenum format,GLenum type,void *pixels)
{
    tg_gles_state *c=tgGlesCurrentState();tg_context *t;unsigned char *out=pixels;int px,py;
    if(!c)return;if(format!=GL_RGBA||type!=GL_UNSIGNED_BYTE){tgGlesSetError(GL_INVALID_ENUM);return;}
    if(width<0||height<0||(!pixels&&width&&height)){tgGlesSetError(GL_INVALID_VALUE);return;}
    if(c->framebuffers&&!tgGlesFramebufferReady(c)){tgGlesSetError(GL_INVALID_FRAMEBUFFER_OPERATION);return;}t=c->target;
    if(x<t->tile_x||y<0||(long long)x+width>t->tile_x+t->surface_width||(long long)y+height>c->logical_height || c->logical_height-y-height<t->tile_y || c->logical_height-y>t->tile_y+t->surface_height){tgGlesSetError(GL_INVALID_VALUE);return;}
    for(py=0;py<height;py++)for(px=0;px<width;px++){
        unsigned int color=t->color[(c->logical_height-1-(y+py)-t->tile_y)*t->surface_pitch+(x+px-t->tile_x)];
        *out++=(unsigned char)(color>>16);*out++=(unsigned char)(color>>8);*out++=(unsigned char)color;*out++=(unsigned char)(color>>24);
    }
}
