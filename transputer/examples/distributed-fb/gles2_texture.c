#include "gles2_texture.h"
static tg_gles_texture_state *state(void)
{
    tg_gles_state *c=tgGlesCurrentState();
    if(!c)return 0;
    if(!c->textures)tgGlesSetError(GL_INVALID_OPERATION);
    return c->textures;
}
static int find(tg_gles_texture_state *s,GLuint name)
{ int i; for(i=0;i<TG_GLES_TEXTURES;i++)if(s->objects[i].reserved && s->objects[i].name==name)return i; return -1; }
void tgGlesInitTextures(tg_gles_state *c,tg_gles_texture_state *s,void *arena,unsigned int bytes)
{
    int i; s->arena=arena; s->slot_size=bytes/TG_GLES_TEXTURES; s->active=0; s->next_name=1;
    for(i=0;i<TG_GLES_TEXTURES;i++)s->objects[i].reserved=s->objects[i].created=0;
    for(i=0;i<TG_GLES_TEXTURE_UNITS;i++)s->bound[i]=0;
    c->textures=s;
}
void glGenTextures(GLsizei n,GLuint *names)
{
    tg_gles_texture_state *s=state(); int i,j,free_count=0;
    if(!s)return; if(n<0 || (n && !names)) { tgGlesSetError(GL_INVALID_VALUE); return; }
    for(i=0;i<TG_GLES_TEXTURES;i++)if(!s->objects[i].reserved)free_count++;
    if(n>free_count) { tgGlesSetError(GL_OUT_OF_MEMORY); return; }
    for(i=0;i<n;i++) {
        for(j=0;j<TG_GLES_TEXTURES;j++)if(!s->objects[j].reserved)break;
        while(!s->next_name || find(s,s->next_name)>=0)s->next_name++;
        s->objects[j].name=names[i]=s->next_name++; s->objects[j].reserved=1; s->objects[j].created=0;
        s->objects[j].size=0; s->objects[j].view.rgba=0; s->objects[j].view.width=s->objects[j].view.height=0;
        s->objects[j].view.wrap_s=s->objects[j].view.wrap_t=TG_FRAG_REPEAT;
    }
}
void glDeleteTextures(GLsizei n,const GLuint *names)
{
    tg_gles_texture_state *s=state(); int i,j,k;
    if(!s)return; if(n<0 || (n && !names)) { tgGlesSetError(GL_INVALID_VALUE); return; }
    for(i=0;i<n;i++)if((j=find(s,names[i]))>=0) {
        for(k=0;k<TG_GLES_TEXTURE_UNITS;k++)if(s->bound[k]==names[i])s->bound[k]=0;
        s->objects[j].reserved=s->objects[j].created=0;
    }
}
void glActiveTexture(GLenum unit)
{
    tg_gles_texture_state *s=state(); if(!s)return;
    if(unit<GL_TEXTURE0 || unit>=GL_TEXTURE0+TG_GLES_TEXTURE_UNITS) { tgGlesSetError(GL_INVALID_ENUM); return; }
    s->active=unit-GL_TEXTURE0;
}
void glBindTexture(GLenum target,GLuint name)
{
    tg_gles_texture_state *s=state(); int slot;
    if(!s)return; if(target!=GL_TEXTURE_2D) { tgGlesSetError(GL_INVALID_ENUM); return; }
    if(name && (slot=find(s,name))<0) { tgGlesSetError(GL_INVALID_OPERATION); return; }
    if(name)s->objects[slot].created=1;
    s->bound[s->active]=name;
}
static tg_gles_texture *bound(tg_gles_texture_state *s)
{
    int i=find(s,s->bound[s->active]); if(i<0) { tgGlesSetError(GL_INVALID_OPERATION); return 0; }
    return &s->objects[i];
}
void glTexParameteri(GLenum target,GLenum pname,GLint value)
{
    tg_gles_texture_state *s=state(); tg_gles_texture *t;
    if(!s)return; if(target!=GL_TEXTURE_2D) { tgGlesSetError(GL_INVALID_ENUM); return; }
    if(!(t=bound(s)))return;
    if(pname==GL_TEXTURE_MIN_FILTER || pname==GL_TEXTURE_MAG_FILTER) {
        if(value!=GL_NEAREST)tgGlesSetError(GL_INVALID_ENUM); return;
    }
    if(pname!=GL_TEXTURE_WRAP_S && pname!=GL_TEXTURE_WRAP_T) { tgGlesSetError(GL_INVALID_ENUM); return; }
    if(value!=GL_REPEAT && value!=GL_CLAMP_TO_EDGE) { tgGlesSetError(GL_INVALID_ENUM); return; }
    if(pname==GL_TEXTURE_WRAP_S)t->view.wrap_s=value==GL_REPEAT?TG_FRAG_REPEAT:TG_FRAG_CLAMP;
    else t->view.wrap_t=value==GL_REPEAT?TG_FRAG_REPEAT:TG_FRAG_CLAMP;
}
void glTexImage2D(GLenum target,GLint level,GLint internal,GLsizei width,GLsizei height,GLint border,GLenum format,GLenum type,const void *pixels)
{
    tg_gles_texture_state *s=state(); tg_gles_texture *t; unsigned int n,i,slot; unsigned char *dst; const unsigned char *src=pixels;
    if(!s)return; if(target!=GL_TEXTURE_2D || format!=GL_RGBA || internal!=GL_RGBA || type!=GL_UNSIGNED_BYTE) { tgGlesSetError(GL_INVALID_ENUM); return; }
    if(level!=0 || border!=0 || width<0 || height<0 || width>32767 || height>32767) { tgGlesSetError(GL_INVALID_VALUE); return; }
    if(!(t=bound(s)))return;
    if((unsigned int)width && (unsigned int)height>s->slot_size/4u/(unsigned int)width) { tgGlesSetError(GL_OUT_OF_MEMORY); return; }
    n=(unsigned int)width*(unsigned int)height*4u; slot=(unsigned int)(t-s->objects);
    if(n && !s->arena) { tgGlesSetError(GL_OUT_OF_MEMORY); return; }
    dst=s->arena?s->arena+slot*s->slot_size:0;
    for(i=0;i<n;i++)dst[i]=src?src[i]:0;
    t->size=n; t->view.rgba=n?dst:0; t->view.width=width; t->view.height=height;
}
const tg_fragment_texture *tgGlesTextureUnit(unsigned int unit)
{
    tg_gles_texture_state *s=state(); int i;
    if(!s || unit>=TG_GLES_TEXTURE_UNITS || (i=find(s,s->bound[unit]))<0 || !s->objects[i].view.rgba)return 0;
    return &s->objects[i].view;
}
