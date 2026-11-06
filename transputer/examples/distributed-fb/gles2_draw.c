#include "gles2_draw.h"
#ifdef __transputer__
typedef signed char int8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
#define SIZE_MAX ((size_t)-1)
#else
#include <stdint.h>
#endif

static tg_gles_draw_state *draw(void)
{
    tg_gles_state *c=tgGlesCurrentState();
    if (!c) return 0;
    if (!c->draw) tgGlesSetError(GL_INVALID_OPERATION);
    return c->draw;
}
static int find(tg_gles_draw_state *s, GLuint name)
{
    int i;
    for (i=0;i<TG_GLES_MAX_BUFFERS;i++) if (s->buffers[i].reserved && s->buffers[i].name==name) return i;
    return -1;
}
static int free_slot(tg_gles_draw_state *s)
{
    int i;
    for (i=0;i<TG_GLES_MAX_BUFFERS;i++) if (!s->buffers[i].reserved) return i;
    return -1;
}
static int target_slot(tg_gles_draw_state *s, GLenum target)
{
    GLuint name;
    if (target==GL_ARRAY_BUFFER) name=s->array_buffer;
    else if (target==GL_ELEMENT_ARRAY_BUFFER) name=s->element_buffer;
    else { tgGlesSetError(GL_INVALID_ENUM); return -1; }
    if (!name || find(s,name)<0) { tgGlesSetError(GL_INVALID_OPERATION); return -1; }
    return find(s,name);
}
static int type_size(GLenum t)
{
    if (t==GL_BYTE || t==GL_UNSIGNED_BYTE) return 1;
    if (t==GL_SHORT || t==GL_UNSIGNED_SHORT) return 2;
    if (t==GL_FIXED || t==GL_FLOAT) return 4;
    return 0;
}
void tgGlesInitDraw(tg_gles_state *c, tg_gles_draw_state *s, void *arena,
                    unsigned int arena_size, tg_gles_vertex_fn vertex, void *user)
{
    int i;
    s->arena=(unsigned char *)arena; s->slot_size=(arena_size/TG_GLES_MAX_BUFFERS)&~3u;
    s->array_buffer=s->element_buffer=0; s->vertex=vertex; s->vertex_user=user; s->persistent_cache=GL_FALSE;
    tgGlesInvalidateVertexCache(s);
    for (i=0;i<TG_GLES_MAX_BUFFERS;i++) {
        s->buffers[i].name=0; s->buffers[i].size=0; s->buffers[i].usage=GL_STATIC_DRAW;
        s->buffers[i].reserved=s->buffers[i].created=0;
    }
    for (i=0;i<TG_GLES_MAX_ATTRIBUTES;i++) {
        s->attributes[i].pointer=0; s->attributes[i].buffer=0; s->attributes[i].size=4;
        s->attributes[i].type=GL_FLOAT; s->attributes[i].stride=0;
        s->attributes[i].normalized=s->attributes[i].enabled=GL_FALSE;
    }
    c->draw=s;
}
void tgGlesInvalidateVertexCache(tg_gles_draw_state *s)
{
    int i; for(i=0;i<TG_GLES_VERTEX_CACHE_SIZE;i++) s->cache[i].valid=0;
}
void glGenBuffers(GLsizei n, GLuint *out)
{
    tg_gles_draw_state *s=draw(); int i,slot; GLuint name;
    if (!s) return;
    if (n<0) { tgGlesSetError(GL_INVALID_VALUE); return; }
    /* Reserve all or none when this bounded implementation runs out. */
    { int avail=0; for(i=0;i<TG_GLES_MAX_BUFFERS;i++) if (!s->buffers[i].reserved) avail++;
      if (n>avail) { tgGlesSetError(GL_OUT_OF_MEMORY); return; } }
    for (i=0;i<n;i++) {
        slot=free_slot(s); name=1;
        while (find(s,name)>=0) name++;
        s->buffers[slot].name=name; s->buffers[slot].reserved=1; s->buffers[slot].created=0;
        out[i]=name;
    }
}
void glDeleteBuffers(GLsizei n, const GLuint *names)
{
    tg_gles_draw_state *s=draw(); int i,j,slot;
    if (!s) return;
    if (n<0) { tgGlesSetError(GL_INVALID_VALUE); return; }
    for (i=0;i<n;i++) {
        if (!names[i] || (slot=find(s,names[i]))<0) continue;
        if (s->array_buffer==names[i]) s->array_buffer=0;
        if (s->element_buffer==names[i]) s->element_buffer=0;
        for (j=0;j<TG_GLES_MAX_ATTRIBUTES;j++) if (s->attributes[j].buffer==names[i]) {
            s->attributes[j].buffer=0; s->attributes[j].pointer=0;
        }
        s->buffers[slot].reserved=s->buffers[slot].created=0; s->buffers[slot].size=0;
    }
    tgGlesInvalidateVertexCache(s);
}
void glBindBuffer(GLenum target, GLuint name)
{
    tg_gles_draw_state *s=draw(); int slot;
    if (!s) return;
    if (target!=GL_ARRAY_BUFFER && target!=GL_ELEMENT_ARRAY_BUFFER) { tgGlesSetError(GL_INVALID_ENUM); return; }
    if (name) {
        slot=find(s,name);
        if (slot<0) {
            slot=free_slot(s);
            if (slot<0) { tgGlesSetError(GL_OUT_OF_MEMORY); return; }
            s->buffers[slot].name=name; s->buffers[slot].size=0;
            s->buffers[slot].usage=GL_STATIC_DRAW; s->buffers[slot].reserved=1;
        }
        s->buffers[slot].created=1;
    }
    if (target==GL_ARRAY_BUFFER) s->array_buffer=name; else s->element_buffer=name;
}
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    tg_gles_draw_state *s=draw(); int slot; unsigned int i; const unsigned char *src=data; unsigned char *dst;
    if (!s) return;
    if (target!=GL_ARRAY_BUFFER && target!=GL_ELEMENT_ARRAY_BUFFER) { tgGlesSetError(GL_INVALID_ENUM); return; }
    if (size<0) { tgGlesSetError(GL_INVALID_VALUE); return; }
    if (usage!=GL_STATIC_DRAW && usage!=GL_DYNAMIC_DRAW && usage!=GL_STREAM_DRAW) { tgGlesSetError(GL_INVALID_ENUM); return; }
    slot=target_slot(s,target); if (slot<0) return;
    if ((size_t)size>s->slot_size || (size && !s->arena)) { tgGlesSetError(GL_OUT_OF_MEMORY); return; }
    if (src && size) {
        dst=s->arena+(unsigned int)slot*s->slot_size;
        for (i=0;i<(unsigned int)size;i++) dst[i]=src[i];
    }
    s->buffers[slot].size=(unsigned int)size; s->buffers[slot].usage=usage;
    tgGlesInvalidateVertexCache(s);
}
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    tg_gles_draw_state *s=draw(); int slot; unsigned int i; const unsigned char *src=data; unsigned char *dst;
    if (!s) return;
    if (target!=GL_ARRAY_BUFFER && target!=GL_ELEMENT_ARRAY_BUFFER) { tgGlesSetError(GL_INVALID_ENUM); return; }
    if (offset<0 || size<0) { tgGlesSetError(GL_INVALID_VALUE); return; }
    slot=target_slot(s,target); if (slot<0) return;
    if ((size_t)offset>s->buffers[slot].size || (size_t)size>s->buffers[slot].size-(size_t)offset) { tgGlesSetError(GL_INVALID_VALUE); return; }
    if (!size) return;
    if (!src) { tgGlesSetError(GL_INVALID_VALUE); return; }
    dst=s->arena+(unsigned int)slot*s->slot_size+(unsigned int)offset;
    for (i=0;i<(unsigned int)size;i++) dst[i]=src[i];
    tgGlesInvalidateVertexCache(s);
}
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
    tg_gles_draw_state *s=draw(); tg_gles_attribute *a;
    if (!s) return;
    if (index>=TG_GLES_MAX_ATTRIBUTES || size<1 || size>4 || stride<0) { tgGlesSetError(GL_INVALID_VALUE); return; }
    if (!type_size(type)) { tgGlesSetError(GL_INVALID_ENUM); return; }
    a=&s->attributes[index]; a->size=size; a->type=type; a->normalized=normalized ? GL_TRUE : GL_FALSE;
    a->stride=stride; a->pointer=pointer; a->buffer=s->array_buffer;
    tgGlesInvalidateVertexCache(s);
}
void glEnableVertexAttribArray(GLuint i)
{
    tg_gles_draw_state *s=draw(); if (!s) return;
    if (i>=TG_GLES_MAX_ATTRIBUTES) { tgGlesSetError(GL_INVALID_VALUE); return; }
    s->attributes[i].enabled=GL_TRUE;
    tgGlesInvalidateVertexCache(s);
}
void glDisableVertexAttribArray(GLuint i)
{
    tg_gles_draw_state *s=draw(); if (!s) return;
    if (i>=TG_GLES_MAX_ATTRIBUTES) { tgGlesSetError(GL_INVALID_VALUE); return; }
    s->attributes[i].enabled=GL_FALSE;
    tgGlesInvalidateVertexCache(s);
}
/* Values are native-endian and may be unaligned; byte copies avoid alignment
 * traps on transputers while preserving the host/guest representation. */
static GLfloat component(const unsigned char *p, GLenum type, GLboolean norm)
{
    union { float f; int32_t i; uint32_t u; unsigned char b[4]; } v;
    int i, n=type_size(type);
    v.u=0; for (i=0;i<n;i++) v.b[i]=p[i];
    if (type==GL_FLOAT) return v.f;
    if (type==GL_FIXED) return (GLfloat)v.i/65536.0f;
    if (type==GL_BYTE) { int x=(int)(int8_t)v.b[0]; return norm ? (x==-128 ? -1.0f : (GLfloat)x/127.0f) : (GLfloat)x; }
    if (type==GL_UNSIGNED_BYTE) return norm ? (GLfloat)v.b[0]/255.0f : (GLfloat)v.b[0];
    if (type==GL_SHORT) { int x=(int)(int16_t)v.u; return norm ? (x==-32768 ? -1.0f : (GLfloat)x/32767.0f) : (GLfloat)x; }
    return norm ? (GLfloat)(uint16_t)v.u/65535.0f : (GLfloat)(uint16_t)v.u;
}
static int vertex(tg_gles_draw_state *s, unsigned int index, tg_screen_vertex *out)
{
    GLfloat values[TG_GLES_MAX_ATTRIBUTES][4]; int i,j;
    for (i=0;i<TG_GLES_MAX_ATTRIBUTES;i++) {
        tg_gles_attribute *a=&s->attributes[i]; const unsigned char *p; size_t offset, stride, bytes;
        values[i][0]=values[i][1]=values[i][2]=0.0f; values[i][3]=1.0f;
        if (!a->enabled) continue;
        bytes=(size_t)a->size*(size_t)type_size(a->type);
        stride=a->stride ? (size_t)a->stride : bytes;
        if (stride && index>(SIZE_MAX-(uintptr_t)a->pointer)/stride) { tgGlesSetError(GL_INVALID_OPERATION); return 0; }
        offset=(uintptr_t)a->pointer+(size_t)index*stride;
        if (a->buffer) {
            int slot=find(s,a->buffer);
            if (slot<0 || offset>s->buffers[slot].size || bytes>s->buffers[slot].size-offset) { tgGlesSetError(GL_INVALID_OPERATION); return 0; }
            p=s->arena+(unsigned int)slot*s->slot_size+offset;
        } else {
            if (!offset) { tgGlesSetError(GL_INVALID_OPERATION); return 0; }
            p=(const unsigned char *)(uintptr_t)offset;
        }
        for (j=0;j<a->size;j++) values[i][j]=component(p+j*type_size(a->type),a->type,a->normalized);
    }
    return s->vertex(values,out,s->vertex_user);
}
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    tg_gles_draw_state *s=draw(); tg_gles_state *c=tgGlesCurrentState();
    const unsigned char *p; size_t offset, bytes; int i,j,width; tg_screen_vertex v[3];
    if (!s) return;
    if (mode!=GL_TRIANGLES) { tgGlesSetError(GL_INVALID_ENUM); return; }
    if (count<0) { tgGlesSetError(GL_INVALID_VALUE); return; }
    if (type!=GL_UNSIGNED_BYTE && type!=GL_UNSIGNED_SHORT) { tgGlesSetError(GL_INVALID_ENUM); return; }
    if (!s->vertex) { tgGlesSetError(GL_INVALID_OPERATION); return; }
    if (!count) return;
    width=(type==GL_UNSIGNED_BYTE) ? 1 : 2;
    if ((size_t)count>SIZE_MAX/(size_t)width) { tgGlesSetError(GL_INVALID_OPERATION); return; }
    bytes=(size_t)count*(size_t)width; offset=(uintptr_t)indices;
    if (s->element_buffer) {
        int slot=find(s,s->element_buffer);
        if (slot<0 || offset>s->buffers[slot].size || bytes>s->buffers[slot].size-offset) { tgGlesSetError(GL_INVALID_OPERATION); return; }
        p=s->arena+(unsigned int)slot*s->slot_size+offset;
    } else {
        if (!indices) { tgGlesSetError(GL_INVALID_OPERATION); return; }
        p=(const unsigned char *)indices;
    }
    /* Client arrays and arbitrary external callbacks can mutate without a GL
     * command. Keep their cache only within this draw; the GLSL program path
     * opts in to cross-draw caching when every enabled attribute is a VBO. */
    if (!s->persistent_cache) tgGlesInvalidateVertexCache(s);
    else for(i=0;i<TG_GLES_MAX_ATTRIBUTES;i++)
        if(s->attributes[i].enabled && !s->attributes[i].buffer) { tgGlesInvalidateVertexCache(s); break; }
    for (i=0;i+2<count;i+=3) {
        for (j=0;j<3;j++) {
            unsigned int index;
            if (width==1) index=p[i+j];
            else { union { uint16_t value; unsigned char b[2]; } x; x.b[0]=p[(i+j)*2]; x.b[1]=p[(i+j)*2+1]; index=x.value; }
#ifdef TG_DISABLE_VERTEX_CACHE
            if (!vertex(s,index,&v[j])) return;
#else
            if (!s->cache[index&(TG_GLES_VERTEX_CACHE_SIZE-1)].valid || s->cache[index&(TG_GLES_VERTEX_CACHE_SIZE-1)].index!=index) {
                tg_gles_vertex_cache_entry *entry=&s->cache[index&(TG_GLES_VERTEX_CACHE_SIZE-1)];
                if (!vertex(s,index,&entry->value)) return;
                entry->index=index; entry->valid=1;
            }
            v[j]=s->cache[index&(TG_GLES_VERTEX_CACHE_SIZE-1)].value;
#endif
        }
        tg_draw_triangle(c->target,&v[0],&v[1],&v[2]);
    }
}
