#include "gles2_program.h"
#ifdef TG_GLES_VM_PROGRAM
#include "gles2_vm_bridge.h"
#endif

/* The compiler below implements one useful GLSL ES grammar, rather than
 * reporting success for shader features the fixed rasterizer cannot run. */
static int same(const char *a, const char *b) { while (*a && *a==*b) { a++; b++; } return *a==*b; }
static void copy(char *dst, const char *src, int cap) { int i=0; if(cap<=0) return; while(i<cap-1 && src[i]) { dst[i]=src[i]; i++; } dst[i]=0; }
static int length(const char *s) { int n=0; while(s[n]) n++; return n; }
static tg_gles_program_state *state(void)
{
    tg_gles_state *c=tgGlesCurrentState();
    if (!c) return 0;
    if (!c->programs) tgGlesSetError(GL_INVALID_OPERATION);
    return c->programs;
}
static tg_gles_shader *shader(tg_gles_program_state *s, GLuint id)
{
    if (!id || id>TG_GLES_MAX_SHADERS || !s->shaders[id-1].used) { tgGlesSetError(GL_INVALID_VALUE); return 0; }
    return &s->shaders[id-1];
}
static tg_gles_program *program(tg_gles_program_state *s, GLuint id)
{
    if (!id || id>TG_GLES_MAX_PROGRAMS || !s->programs[id-1].used) { tgGlesSetError(GL_INVALID_VALUE); return 0; }
    return &s->programs[id-1];
}
void tgGlesInitPrograms(tg_gles_state *c, tg_gles_program_state *s)
{
    int i; s->current_program=0;
    for(i=0;i<TG_GLES_MAX_SHADERS;i++) { s->shaders[i].used=0; s->shaders[i].source[0]=0; }
    for(i=0;i<TG_GLES_MAX_PROGRAMS;i++) s->programs[i].used=0;
    c->programs=s;
}
GLuint glCreateShader(GLenum type)
{
    tg_gles_program_state *s=state(); int i;
    if(!s) return 0;
    if(type!=GL_VERTEX_SHADER && type!=GL_FRAGMENT_SHADER) { tgGlesSetError(GL_INVALID_ENUM); return 0; }
    for(i=0;i<TG_GLES_MAX_SHADERS;i++) if(!s->shaders[i].used) {
        tg_gles_shader *h=&s->shaders[i]; h->used=1; h->type=type; h->compiled=h->overflow=h->deleted=0;
        h->source[0]=h->attribute[0]=h->matrix[0]=h->color[0]=0; h->log=""; return (GLuint)i+1;
    }
    tgGlesSetError(GL_OUT_OF_MEMORY); return 0;
}
void glShaderSource(GLuint id, GLsizei count, const GLchar *const *strings, const GLint *lengths)
{
    tg_gles_program_state *s=state(); tg_gles_shader *h; int i,j,n=0;
    if(!s || !(h=shader(s,id))) return;
    if(count<0) { tgGlesSetError(GL_INVALID_VALUE); return; }
    h->overflow=0; h->compiled=0;
    for(i=0;i<count;i++) {
        if(!strings || !strings[i]) { tgGlesSetError(GL_INVALID_VALUE); return; }
        for(j=0;(lengths && lengths[i]>=0) ? j<lengths[i] : strings[i][j]!=0;j++) {
            if(n<TG_GLES_SHADER_SOURCE_MAX-1) h->source[n++]=strings[i][j]; else h->overflow=1;
        }
    }
    h->source[n]=0;
}
typedef struct { const char *p; char token[64]; int bad; } lexer;
static int isletter(char c) { return (c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_'; }
static int isdigit0(char c) { return c>='0' && c<='9'; }
static void next(lexer *l)
{
    int n=0;
    for (;;) {
        while(*l->p==' ' || *l->p=='\t' || *l->p=='\n' || *l->p=='\r') l->p++;
        if(l->p[0]=='/' && l->p[1]=='/') { while(*l->p && *l->p!='\n') l->p++; continue; }
        if(l->p[0]=='/' && l->p[1]=='*') {
            l->p+=2; while(*l->p && !(l->p[0]=='*' && l->p[1]=='/')) l->p++;
            if(!*l->p) { l->bad=1; break; } l->p+=2; continue;
        }
        break;
    }
    if(isletter(*l->p)) {
        while(isletter(*l->p) || isdigit0(*l->p)) { if(n<63) l->token[n++]=*l->p; else l->bad=1; l->p++; }
    } else if(isdigit0(*l->p)) {
        while(isdigit0(*l->p) || *l->p=='.') { if(n<63) l->token[n++]=*l->p; else l->bad=1; l->p++; }
    } else if(*l->p) l->token[n++]=*l->p++;
    l->token[n]=0;
}
static int take(lexer *l, const char *expected) { if(l->bad || !same(l->token,expected)) return 0; next(l); return 1; }
static int identifier(lexer *l, char *name)
{
    if(l->bad || !isletter(l->token[0]) || length(l->token)>=32) return 0;
    copy(name,l->token,32); next(l); return 1;
}
/* Small object-like feature-flag preprocessor: no macro substitution,
 * expressions or function-like macros. Unsupported directives fail closed. */
static int preprocess(const char *src,char *out,int cap)
{
    char names[16][32]; int nname=0,depth=0,active=1,n=0,in_comment=0;
    unsigned char parent[16],condition[16],seen_else[16];
    while(*src) {
        const char *line=src,*end,*p; int directive=0;
        while(*src && *src!='\n') src++; end=src; if(*src)src++;
        p=line; while(p<end && (*p==' ' || *p=='\t' || *p=='\r'))p++;
        if(!in_comment && p<end && *p=='#') {
            char op[16],name[32]; int k=0,i,found=0; directive=1; p++;
            while(p<end && (*p==' ' || *p=='\t'))p++;
            while(p<end && isletter(*p)) { if(k>=15)return 0; op[k++]=*p++; } op[k]=0;
            while(p<end && (*p==' ' || *p=='\t'))p++;
            k=0; while(p<end && (isletter(*p) || isdigit0(*p))) { if(k>=31)return 0; name[k++]=*p++; } name[k]=0;
            while(p<end && (*p==' ' || *p=='\t' || *p=='\r'))p++;
            if(p<end && !(p+1<end && p[0]=='/' && p[1]=='/'))return 0;
            for(i=0;i<nname;i++) if(same(names[i],name))found=1;
            if(same(op,"define")) {
                if(!isletter(name[0]))return 0;
                if(active && !found) { if(nname>=16)return 0; copy(names[nname++],name,32); }
            } else if(same(op,"undef")) {
                if(!isletter(name[0]))return 0;
                if(active && found) { for(i=0;i<nname;i++)if(same(names[i],name)){nname--;if(i<nname)copy(names[i],names[nname],32);break;} }
            } else if(same(op,"ifdef") || same(op,"ifndef")) {
                if(!isletter(name[0]) || depth>=16)return 0;
                parent[depth]=(unsigned char)active; condition[depth]=(unsigned char)(same(op,"ifdef")?found:!found);
                seen_else[depth]=0; active=active && condition[depth]; depth++;
            } else if(same(op,"else")) {
                if(name[0] || !depth || seen_else[depth-1])return 0;
                seen_else[depth-1]=1; active=parent[depth-1] && !condition[depth-1];
            } else if(same(op,"endif")) {
                if(name[0] || !depth)return 0; depth--; active=parent[depth];
            } else return 0;
        }
        if(!directive) {
            p=line;
            while(p<end) {
                if(!in_comment && p+1<end && p[0]=='/' && p[1]=='/') break;
                if(!in_comment && p+1<end && p[0]=='/' && p[1]=='*') { if(active) { if(n>=cap-1)return 0; out[n++]=' '; } in_comment=1; p+=2; continue; }
                if(in_comment && p+1<end && p[0]=='*' && p[1]=='/') { in_comment=0; p+=2; continue; }
                if(!in_comment && active) { if(n>=cap-1)return 0; out[n++]=*p; }
                p++;
            }
        }
        if(n>=cap-1)return 0; out[n++]='\n';
    }
    if(depth || in_comment)return 0; out[n]=0; return 1;
}
static int compile(tg_gles_shader *h,const char *source)
{
    lexer l; int attr_size=0, have_attr=0, have_uniform=0, wrapped=0;
    char attr[32], uniform[32];
    l.p=source; l.bad=0; next(&l);
    if(same(l.token,"precision")) {
        next(&l); if(!same(l.token,"lowp") && !same(l.token,"mediump") && !same(l.token,"highp")) return 0;
        next(&l); if(!take(&l,"float") || !take(&l,";")) return 0;
    }
    while(same(l.token,"attribute") || same(l.token,"uniform")) {
        int is_attr=same(l.token,"attribute"); next(&l);
        if(is_attr) {
            if(h->type!=GL_VERTEX_SHADER || have_attr) return 0;
            if(same(l.token,"vec3")) attr_size=3; else if(same(l.token,"vec4")) attr_size=4; else return 0;
            next(&l); if(!identifier(&l,attr)) return 0; have_attr=1;
        } else {
            if(have_uniform || !take(&l,h->type==GL_VERTEX_SHADER ? "mat4" : "vec4")) return 0;
            if(!identifier(&l,uniform)) return 0;
            have_uniform=1;
        }
        if(!take(&l,";")) return 0;
    }
    if(!have_uniform || (h->type==GL_VERTEX_SHADER && !have_attr)) return 0;
    if(!take(&l,"void") || !take(&l,"main") || !take(&l,"(")) return 0;
    if(same(l.token,"void")) next(&l);
    if(!take(&l,")") || !take(&l,"{")) return 0;
    if(h->type==GL_VERTEX_SHADER) {
        if(!take(&l,"gl_Position") || !take(&l,"=") || !take(&l,uniform) || !take(&l,"*")) return 0;
        if(same(l.token,"vec4")) { next(&l); if(!take(&l,"(")) return 0; wrapped=1; }
        if(!take(&l,attr)) return 0;
        if(wrapped && (!take(&l,",") || (!take(&l,"1.0") && !take(&l,"1")) || !take(&l,")"))) return 0;
        if((attr_size==3 && !wrapped) || (attr_size==4 && wrapped)) return 0;
        copy(h->attribute,attr,32); copy(h->matrix,uniform,32);
    } else {
        if(!take(&l,"gl_FragColor") || !take(&l,"=") || !take(&l,uniform)) return 0;
        copy(h->color,uniform,32);
    }
    return take(&l,";") && take(&l,"}") && !l.bad && !l.token[0];
}
void glCompileShader(GLuint id)
{
    static char preprocessed[TG_GLES_SHADER_SOURCE_MAX];
    tg_gles_program_state *s=state(); tg_gles_shader *h; int pp;
    if(!s || !(h=shader(s,id))) return;
    pp=!h->overflow && preprocess(h->source,preprocessed,sizeof(preprocessed));
    h->compiled=(pp && compile(h,preprocessed)) ? GL_TRUE : GL_FALSE;
#ifdef TG_GLES_VM_PROGRAM
    h->vm_compiled=GL_FALSE;
    if(pp && !h->compiled) {
        int ok=h->type==GL_VERTEX_SHADER ? tg_glsl_compile_vertex(preprocessed,&h->ir) : tg_glsl_compile_fragment(preprocessed,&h->ir);
        if(ok) { h->compiled=GL_TRUE; h->vm_compiled=GL_TRUE; }
    }
#endif
    h->log=h->compiled ? "" : (h->overflow ? "shader source exceeds implementation limit" : (!pp ? "unsupported or malformed shader preprocessor input" : "unsupported GLSL ES shader syntax or operations"));
}
void glGetShaderiv(GLuint id, GLenum pname, GLint *params)
{
    tg_gles_program_state *s=state(); tg_gles_shader *h; if(!s || !(h=shader(s,id))) return;
    if(pname==GL_COMPILE_STATUS) *params=h->compiled;
    else if(pname==GL_INFO_LOG_LENGTH) *params=h->log[0] ? length(h->log)+1 : 0;
    else if(pname==GL_SHADER_TYPE) *params=(GLint)h->type;
    else if(pname==GL_SHADER_SOURCE_LENGTH) *params=h->source[0] ? length(h->source)+1 : 0;
    else if(pname==GL_DELETE_STATUS) *params=h->deleted;
    else tgGlesSetError(GL_INVALID_ENUM);
}
static void info_log(const char *src, GLsizei cap, GLsizei *outlen, GLchar *out)
{
    int i=0; if(cap<0) { tgGlesSetError(GL_INVALID_VALUE); return; }
    if(cap>0) { while(i<cap-1 && src[i]) { out[i]=src[i]; i++; } out[i]=0; }
    if(outlen) *outlen=i;
}
void glGetShaderInfoLog(GLuint id, GLsizei cap, GLsizei *n, GLchar *out)
{
    tg_gles_program_state *s=state(); tg_gles_shader *h; if(s && (h=shader(s,id))) info_log(h->log,cap,n,out);
}
static void reap_shader(tg_gles_program_state *s, GLuint id)
{
    int i; tg_gles_shader *h;
    if(!id || id>TG_GLES_MAX_SHADERS) return;
    h=&s->shaders[id-1]; if(!h->used || !h->deleted) return;
    for(i=0;i<TG_GLES_MAX_PROGRAMS;i++)
        if(s->programs[i].used && (s->programs[i].vertex_shader==id || s->programs[i].fragment_shader==id)) return;
    h->used=0;
}
void glDeleteShader(GLuint id)
{
    tg_gles_program_state *s=state(); tg_gles_shader *h;
    if(!s || !id || !(h=shader(s,id))) return;
    h->deleted=1; reap_shader(s,id);
}
GLuint glCreateProgram(void)
{
    tg_gles_program_state *s=state(); int i,j; if(!s) return 0;
    for(i=0;i<TG_GLES_MAX_PROGRAMS;i++) if(!s->programs[i].used) {
        tg_gles_program *p=&s->programs[i]; p->used=1; p->linked=p->deleted=0; p->vertex_shader=p->fragment_shader=0;
        p->attribute_index=0; p->bound_attribute[0]=p->attribute[0]=p->matrix_name[0]=p->color_name[0]=0;
        for(j=0;j<16;j++) p->matrix[j]=0.0f;
        for(j=0;j<4;j++) p->color[j]=0.0f;
        for(j=0;j<64;j++) p->texture[j]=0;
        p->log=""; return (GLuint)i+1;
    }
    tgGlesSetError(GL_OUT_OF_MEMORY); return 0;
}
void glAttachShader(GLuint pid, GLuint sid)
{
    tg_gles_program_state *s=state(); tg_gles_program *p; tg_gles_shader *h; GLuint *slot;
    if(!s || !(p=program(s,pid)) || !(h=shader(s,sid))) return;
    slot=h->type==GL_VERTEX_SHADER ? &p->vertex_shader : &p->fragment_shader;
    if(*slot) { tgGlesSetError(GL_INVALID_OPERATION); return; } *slot=sid;
}
void glDetachShader(GLuint pid, GLuint sid)
{
    tg_gles_program_state *s=state(); tg_gles_program *p; tg_gles_shader *h; GLuint *slot;
    if(!s || !(p=program(s,pid)) || !(h=shader(s,sid))) return;
    slot=h->type==GL_VERTEX_SHADER ? &p->vertex_shader : &p->fragment_shader;
    if(*slot!=sid) { tgGlesSetError(GL_INVALID_OPERATION); return; }
    *slot=0; reap_shader(s,sid);
}
void glBindAttribLocation(GLuint id, GLuint index, const GLchar *name)
{
    tg_gles_program_state *s=state(); tg_gles_program *p;
    if(!s || !(p=program(s,id))) return;
    if(index>=TG_GLES_MAX_ATTRIBUTES) { tgGlesSetError(GL_INVALID_VALUE); return; }
    if(!name || length(name)>=32) { tgGlesSetError(GL_INVALID_VALUE); return; }
    p->attribute_index=index; copy(p->bound_attribute,name,32);
}
void glLinkProgram(GLuint id)
{
    tg_gles_program_state *s=state(); tg_gles_program *p; tg_gles_shader *v,*f;
    if(!s || !(p=program(s,id))) return;
    p->linked=0;
    if(!p->vertex_shader || !p->fragment_shader) { p->log="vertex and fragment shaders required"; return; }
    v=&s->shaders[p->vertex_shader-1]; f=&s->shaders[p->fragment_shader-1];
    if(!v->compiled || !f->compiled) { p->log="attached shader did not compile"; return; }
#ifdef TG_GLES_VM_PROGRAM
    p->vm_linked=GL_FALSE;
    if(v->vm_compiled || f->vm_compiled) {
        int i,j;
        if(!v->vm_compiled || !f->vm_compiled) { p->log="shader backends do not match"; return; }
        for(i=0;i<f->ir.symbol_count;i++) if(f->ir.symbols[i].kind==TG_GLSL_VARYING) {
            j=tg_glsl_find_symbol(&v->ir,f->ir.symbols[i].name);
            if(j<0 || v->ir.symbols[j].kind!=TG_GLSL_VARYING || v->ir.symbols[j].width!=f->ir.symbols[i].width) { p->log="varying interface mismatch"; return; }
        }
        for(i=0;i<f->ir.symbol_count;i++) if(f->ir.symbols[i].kind==TG_GLSL_UNIFORM || f->ir.symbols[i].kind==TG_GLSL_SAMPLER) {
            j=tg_glsl_find_symbol(&v->ir,f->ir.symbols[i].name);
            if(j>=0 && ((v->ir.symbols[j].kind!=f->ir.symbols[i].kind) || (v->ir.symbols[j].width!=f->ir.symbols[i].width))) { p->log="uniform interface mismatch"; return; }
        }
        p->vertex_ir=v->ir;p->fragment_ir=f->ir;
        if(!tgGlesVmLink(p)) { p->log="unsupported programmable shader interface";return; }
        p->vm_linked=GL_TRUE;p->linked=GL_TRUE;p->log="";return;
    }
#endif
    copy(p->attribute,v->attribute,32); copy(p->matrix_name,v->matrix,32); copy(p->color_name,f->color,32);
    if(p->bound_attribute[0] && !same(p->bound_attribute,p->attribute)) p->attribute_index=0;
    p->linked=1; p->log="";
}
void glGetProgramiv(GLuint id, GLenum pname, GLint *params)
{
    tg_gles_program_state *s=state(); tg_gles_program *p; if(!s || !(p=program(s,id))) return;
    if(pname==GL_LINK_STATUS) *params=p->linked;
    else if(pname==GL_DELETE_STATUS) *params=p->deleted;
    else if(pname==GL_INFO_LOG_LENGTH) *params=p->log[0] ? length(p->log)+1 : 0;
    else tgGlesSetError(GL_INVALID_ENUM);
}
void glGetProgramInfoLog(GLuint id, GLsizei cap, GLsizei *n, GLchar *out)
{
    tg_gles_program_state *s=state(); tg_gles_program *p; if(s && (p=program(s,id))) info_log(p->log,cap,n,out);
}
static int program_vertex(const GLfloat a[TG_GLES_MAX_ATTRIBUTES][4], tg_screen_vertex *out, void *user)
{
    tg_gles_program *p=user; const GLfloat *v=a[p->attribute_index]; GLfloat x,y,z,w;
    x=p->matrix[0]*v[0]+p->matrix[4]*v[1]+p->matrix[8]*v[2]+p->matrix[12]*v[3];
    y=p->matrix[1]*v[0]+p->matrix[5]*v[1]+p->matrix[9]*v[2]+p->matrix[13]*v[3];
    z=p->matrix[2]*v[0]+p->matrix[6]*v[1]+p->matrix[10]*v[2]+p->matrix[14]*v[3];
    w=p->matrix[3]*v[0]+p->matrix[7]*v[1]+p->matrix[11]*v[2]+p->matrix[15]*v[3];
    /* No homogeneous clipping yet: only a safe, bounded interior works. */
    if(w<=0.01f || x < -w || x > w || y < -w || y > w || z < -w || z > w) return 0;
    out->x=80+(int)(x*80.0f/w); out->y=60-(int)(y*60.0f/w);
    /* The only supported fragment shader is a uniform color, so texture
     * interpolation is irrelevant and z can carry inverted window depth. */
    out->z=(int)((1.0f-z/w)*128.0f); out->u=out->v=0; return 1;
}
static void release_program(tg_gles_program_state *s, GLuint id)
{
    tg_gles_program *p=&s->programs[id-1]; GLuint v=p->vertex_shader, f=p->fragment_shader;
    p->used=0; p->vertex_shader=p->fragment_shader=0;
    reap_shader(s,v); reap_shader(s,f);
}
void glDeleteProgram(GLuint id)
{
    tg_gles_program_state *s=state(); tg_gles_program *p;
    if(!s || !id || !(p=program(s,id))) return;
    p->deleted=1; if(s->current_program!=id) release_program(s,id);
}
void glUseProgram(GLuint id)
{
    tg_gles_state *c=tgGlesCurrentState(); tg_gles_program_state *s=state(); tg_gles_program *p; GLuint old;
    if(!s) return;
    if(!c->draw) { tgGlesSetError(GL_INVALID_OPERATION); return; }
    if(!id) {
        old=s->current_program; s->current_program=0; c->draw->vertex=0; c->draw->persistent_cache=0;
#ifdef TG_GLES_VM_PROGRAM
        c->draw->material_vertex=0;c->draw->material_fragment=0;c->draw->material_prepare=0;c->draw->material_user=0;
#endif
        tgGlesInvalidateVertexCache(c->draw);
        if(old && s->programs[old-1].deleted) release_program(s,old);
        return;
    }
    p=program(s,id); if(!p) return;
    if(!p->linked) { tgGlesSetError(GL_INVALID_OPERATION); return; }
#ifdef TG_GLES_VM_PROGRAM
    if(p->vm_linked) {
        old=s->current_program;s->current_program=id;tgGlesVmUse(p,c->draw);
        if(old && old!=id && s->programs[old-1].deleted)release_program(s,old);
        return;
    }
    c->draw->material_vertex=0;c->draw->material_fragment=0;c->draw->material_prepare=0;c->draw->material_user=0;
#endif
    old=s->current_program; s->current_program=id; c->draw->vertex=program_vertex; c->draw->vertex_user=p; c->draw->persistent_cache=1; c->target->texture=p->texture;
    tgGlesInvalidateVertexCache(c->draw);
    if(old && old!=id && s->programs[old-1].deleted) release_program(s,old);
}
GLint glGetAttribLocation(GLuint id, const GLchar *name)
{
    tg_gles_program_state *s=state(); tg_gles_program *p; if(!s || !(p=program(s,id))) return -1;
    if(!p->linked) { tgGlesSetError(GL_INVALID_OPERATION); return -1; }
#ifdef TG_GLES_VM_PROGRAM
    if(p->vm_linked)return tgGlesVmAttrib(p,name);
#endif
    return name && same(name,p->attribute) ? (GLint)p->attribute_index : -1;
}
GLint glGetUniformLocation(GLuint id, const GLchar *name)
{
    tg_gles_program_state *s=state(); tg_gles_program *p; if(!s || !(p=program(s,id))) return -1;
    if(!p->linked) { tgGlesSetError(GL_INVALID_OPERATION); return -1; }
#ifdef TG_GLES_VM_PROGRAM
    if(p->vm_linked)return tgGlesVmUniformLocation(p,name);
#endif
    if(name && same(name,p->matrix_name)) return 0;
    if(name && same(name,p->color_name)) return 1;
    return -1;
}
void glUniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    tg_gles_program_state *s=state(); tg_gles_program *p; int i;
    if(!s) return;
    if(count<0 || transpose) { tgGlesSetError(GL_INVALID_VALUE); return; }
    if(!s->current_program) { tgGlesSetError(GL_INVALID_OPERATION); return; }
    if(loc==-1 || !count) return;
#ifdef TG_GLES_VM_PROGRAM
    p=&s->programs[s->current_program-1];
    if(p->vm_linked) { if(count!=1 || !value || !tgGlesVmUniformMatrix(p,loc,value))tgGlesSetError(GL_INVALID_OPERATION);return; }
#endif
    if(loc!=0 || count!=1 || !value) { tgGlesSetError(GL_INVALID_OPERATION); return; }
    p=&s->programs[s->current_program-1]; for(i=0;i<16;i++) p->matrix[i]=value[i];
    tgGlesInvalidateVertexCache(tgGlesCurrentState()->draw);
}
static unsigned int color_channel(GLfloat x)
{
    if(x<0.0f) x=0.0f;
    if(x>1.0f) x=1.0f;
    return (unsigned int)(x*255.0f+0.5f);
}
void glUniform4f(GLint loc, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    tg_gles_program_state *s=state(); tg_gles_program *p; unsigned int color; int i;
    if(!s) return;
    if(!s->current_program) { tgGlesSetError(GL_INVALID_OPERATION); return; }
    if(loc==-1) return;
#ifdef TG_GLES_VM_PROGRAM
    p=&s->programs[s->current_program-1];
    if(p->vm_linked) { GLfloat values[4];values[0]=x;values[1]=y;values[2]=z;values[3]=w;if(!tgGlesVmUniformVector(p,loc,values,4))tgGlesSetError(GL_INVALID_OPERATION);return; }
#endif
    if(loc!=1) { tgGlesSetError(GL_INVALID_OPERATION); return; }
    p=&s->programs[s->current_program-1]; p->color[0]=x; p->color[1]=y; p->color[2]=z; p->color[3]=w;
    color=(color_channel(w)<<24)|(color_channel(x)<<16)|(color_channel(y)<<8)|color_channel(z);
    for(i=0;i<64;i++) p->texture[i]=color;
}
#ifdef TG_GLES_VM_PROGRAM
void glUniform3fv(GLint loc,GLsizei count,const GLfloat *value)
{
    tg_gles_program_state *s=state();tg_gles_program *p;if(!s)return;
    if(count<0){tgGlesSetError(GL_INVALID_VALUE);return;}if(!s->current_program){tgGlesSetError(GL_INVALID_OPERATION);return;}if(loc==-1||!count)return;
    p=&s->programs[s->current_program-1];if(!p->vm_linked||count!=1||!value||!tgGlesVmUniformVector(p,loc,value,3))tgGlesSetError(GL_INVALID_OPERATION);
}
void glUniform1f(GLint loc,GLfloat value)
{
    tg_gles_program_state *s=state();tg_gles_program *p;if(!s)return;
    if(!s->current_program){tgGlesSetError(GL_INVALID_OPERATION);return;}if(loc==-1)return;
    p=&s->programs[s->current_program-1];if(!p->vm_linked||!tgGlesVmUniformVector(p,loc,&value,1))tgGlesSetError(GL_INVALID_OPERATION);
}
void glUniform1i(GLint loc,GLint value)
{
    tg_gles_program_state *s=state();tg_gles_program *p;if(!s)return;
    if(!s->current_program){tgGlesSetError(GL_INVALID_OPERATION);return;}if(loc==-1)return;
    p=&s->programs[s->current_program-1];if(!p->vm_linked||!tgGlesVmUniformSampler(p,loc,value))tgGlesSetError(GL_INVALID_OPERATION);
}
#endif
