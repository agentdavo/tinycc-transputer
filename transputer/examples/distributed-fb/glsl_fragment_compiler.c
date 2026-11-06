#include "glsl_fragment_compiler.h"
typedef struct { const char *p; char tok[32]; int bad; tg_glsl_fragment_program *out; unsigned int samplers; } parser;
typedef struct { int reg,width; } expr;
static int letter(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static int digit(char c) { return c>='0'&&c<='9'; }
static int eq(const char *a,const char *b) { while(*a&&*a==*b){a++;b++;} return *a==*b; }
static void next(parser *p)
{
    int n=0;
    for(;;) {
        while(*p->p==' '||*p->p=='\n'||*p->p=='\r'||*p->p=='\t')p->p++;
        if(p->p[0]=='/'&&p->p[1]=='/') { while(*p->p&&*p->p!='\n')p->p++; continue; }
        if(p->p[0]=='/'&&p->p[1]=='*') { p->p+=2; while(*p->p&&!(p->p[0]=='*'&&p->p[1]=='/'))p->p++; if(!*p->p){p->bad=1;break;} p->p+=2; continue; }
        break;
    }
    if(letter(*p->p)) while(letter(*p->p)||digit(*p->p)) { if(n<31)p->tok[n++]=*p->p; else p->bad=1; p->p++; }
    else if(digit(*p->p)) while(digit(*p->p)||*p->p=='.') { if(n<31)p->tok[n++]=*p->p; else p->bad=1; p->p++; }
    else if(*p->p)p->tok[n++]=*p->p++;
    p->tok[n]=0;
}
static int take(parser *p,const char *s) { if(p->bad||!eq(p->tok,s)){p->bad=1;return 0;} next(p);return 1; }
static int alloc(parser *p) { if(p->out->register_count>=TG_VM_REGS){p->bad=1;return 0;} return p->out->register_count++; }
int tg_glsl_find_symbol(const tg_glsl_fragment_program *p,const char *name)
{ int i;if(!p||!name)return -1;for(i=0;i<p->symbol_count;i++)if(eq(p->symbols[i].name,name))return i;return -1; }
static int width(const char *s) { if(eq(s,"float"))return 1;if(eq(s,"vec2"))return 2;if(eq(s,"vec3"))return 3;if(eq(s,"vec4"))return 4;return 0; }
static void emit(parser *p,int op,int d,int a,int b,int w,int flags,int aux)
{
    tg_vm_instruction *i;
    if(p->out->instruction_count>=TG_VM_MAX_INSTRUCTIONS){p->bad=1;return;}
    i=&p->out->code[p->out->instruction_count++];i->op=op;i->dst=d;i->a=a;i->b=b;i->width=w;i->flags=flags;i->aux=aux;
}
static expr expression(parser *p);
static expr primary(parser *p)
{
    expr e={0,0}; int i;
    if(eq(p->tok,"(")){next(p);e=expression(p);take(p,")");return e;}
    if(eq(p->tok,"texture2D")) {
        int sampler; next(p);take(p,"(");sampler=tg_glsl_find_symbol(p->out,p->tok);
        if(sampler<0||p->out->symbols[sampler].kind!=TG_GLSL_SAMPLER){p->bad=1;return e;}
        next(p);take(p,",");e=expression(p);take(p,")");
        if(e.width!=2){p->bad=1;return e;}
        i=alloc(p);emit(p,TG_VM_TEXTURE2D,i,e.reg,0,4,0,p->out->symbols[sampler].slot);e.reg=i;e.width=4;return e;
    }
    if(digit(p->tok[0])) {
        float v=0,scale=1; int frac=0; const char *s=p->tok;
        while(*s){if(*s=='.'){if(frac){p->bad=1;return e;}frac=1;}else {if(frac)scale*=0.1f;v=v*10+(*s-'0');}s++;}
        e.reg=alloc(p);e.width=1;p->out->initial[e.reg][0]=v*scale;next(p);return e;
    }
    i=tg_glsl_find_symbol(p->out,p->tok);
    if(i<0||p->out->symbols[i].kind==TG_GLSL_SAMPLER){p->bad=1;return e;}
    e.reg=p->out->symbols[i].slot;e.width=p->out->symbols[i].width;next(p);return e;
}
static expr product(parser *p)
{
    expr a=primary(p);
    while(!p->bad&&(eq(p->tok,"*")||eq(p->tok,"/"))) {
        int op=eq(p->tok,"*")?TG_VM_MUL:TG_VM_DIV;expr b;int w,d,flags;next(p);b=primary(p);
        if(a.width!=b.width&&a.width!=1&&b.width!=1){p->bad=1;return a;}
        w=a.width>b.width?a.width:b.width; flags=(a.width==1&&w>1?1:0)|(b.width==1&&w>1?2:0);
        d=alloc(p);emit(p,op,d,a.reg,b.reg,w,flags,0);a.reg=d;a.width=w;
    }
    return a;
}
static expr expression(parser *p)
{
    expr a=product(p);
    while(!p->bad&&(eq(p->tok,"+")||eq(p->tok,"-"))) {
        int op=eq(p->tok,"+")?TG_VM_ADD:TG_VM_SUB;expr b;int w,d,flags;next(p);b=product(p);
        if(a.width!=b.width&&a.width!=1&&b.width!=1){p->bad=1;return a;}
        w=a.width>b.width?a.width:b.width; flags=(a.width==1&&w>1?1:0)|(b.width==1&&w>1?2:0);
        d=alloc(p);emit(p,op,d,a.reg,b.reg,w,flags,0);a.reg=d;a.width=w;
    }
    return a;
}
int tg_glsl_compile_fragment(const char *source,tg_glsl_fragment_program *out)
{
    parser p; int i,j; expr e;
    if(!source||!out)return 0;
    out->symbol_count=out->register_count=0;out->instruction_count=0;out->output_register=0;
    for(i=0;i<TG_VM_REGS;i++)for(j=0;j<4;j++)out->initial[i][j]=0;
    p.p=source;p.bad=0;p.out=out;p.samplers=0;next(&p);
    if(eq(p.tok,"precision")){next(&p);if(!eq(p.tok,"lowp")&&!eq(p.tok,"mediump")&&!eq(p.tok,"highp"))return 0;next(&p);take(&p,"float");take(&p,";");}
    while(eq(p.tok,"uniform")||eq(p.tok,"varying")) {
        int kind=eq(p.tok,"uniform")?TG_GLSL_UNIFORM:TG_GLSL_VARYING,w;tg_glsl_symbol *s;
        next(&p);w=width(p.tok);
        if(eq(p.tok,"sampler2D")&&kind==TG_GLSL_UNIFORM){kind=TG_GLSL_SAMPLER;w=0;}
        else if(!w)return 0;
        next(&p);if(!letter(p.tok[0])||out->symbol_count>=TG_GLSL_SYMBOLS||tg_glsl_find_symbol(out,p.tok)>=0)return 0;
        s=&out->symbols[out->symbol_count++];i=0;while(p.tok[i]){s->name[i]=p.tok[i];i++;}s->name[i]=0;s->kind=kind;s->width=w;
        if(kind==TG_GLSL_SAMPLER){if(p.samplers>=TG_VM_TEXTURES)return 0;s->slot=p.samplers++;} else s->slot=alloc(&p);
        next(&p);take(&p,";");if(p.bad)return 0;
    }
    take(&p,"void");take(&p,"main");take(&p,"(");if(eq(p.tok,"void"))next(&p);take(&p,")");take(&p,"{");
    take(&p,"gl_FragColor");take(&p,"=");e=expression(&p);take(&p,";");take(&p,"}");
    if(p.bad||p.tok[0]||e.width!=4)return 0;
    out->output_register=e.reg;return 1;
}
void tg_glsl_fragment_init(const tg_glsl_fragment_program *p,tg_vm_state *s)
{ int i,j;for(i=0;i<TG_VM_REGS;i++)for(j=0;j<4;j++)s->reg[i][j]=p->initial[i][j];for(i=0;i<TG_VM_TEXTURES;i++)s->texture[i]=0;s->sqrt_fn=0;s->pow_fn=0; }
