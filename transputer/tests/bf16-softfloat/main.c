/* __bf16 casts deliberately exercise the compiler's language lowering. */
#ifndef VECTOR_HEADER
#define VECTOR_HEADER "vectors.h"
#endif
#include VECTOR_HEADER
#ifndef VECTOR_COUNT
#define VECTOR_COUNT (sizeof(vectors)/sizeof(vectors[0]))
#define VECTOR_INPUT(i) vectors[i].input
#define VECTOR_WANT(i) vectors[i].want
#endif
#ifndef TIMING_UNIT
#define TIMING_UNIT "instructions"
#endif
#ifdef BINARY16
#define __bf16 _Float16
#define FORMAT_LABEL "F16"
#else
#define FORMAT_LABEL "BF16"
#endif
static unsigned expected_widen(unsigned x){
#ifdef BINARY16
    unsigned sign=(x&0x8000)<<16, e=(x>>10)&31, m=x&1023, n=0, t=m;
    if(e==31)return sign|0x7f800000|(m<<13);
    if(e)return sign|((e+112)<<23)|(m<<13);
    if(!m)return sign;
    while(t){n++;t>>=1;}
    return sign|((102+n)<<23)|((m<<(24-n))&0x7fffff);
#else
    return x<<16;
#endif
}
#define DATA (*(volatile unsigned *)0x10000000u)
#define STATUS (*(volatile unsigned *)0x10000004u)
static void out(unsigned c){while(!(STATUS&1)){} DATA=c;}
static void text(const char *s){while(*s)out(*s++);}
static void hex(unsigned x){int n;for(n=28;n>=0;n-=4)out("0123456789abcdef"[(x>>n)&15]);}
unsigned widen(unsigned x){union { __bf16 f; unsigned short u; } b; union {float f;unsigned u;} r;b.u=x;r.f=(float)b.f;return r.u;}
unsigned narrow(unsigned x){union {float f;unsigned u;} a;union {__bf16 f;unsigned short u;} r;a.u=x;r.f=(__bf16)a.f;return r.u;}
#ifdef MEMORY_PATHS
unsigned narrow_indirect(volatile float *p){union {__bf16 f;unsigned short u;} r;r.f=(__bf16)*p;return r.u;}
#define PACKED_PATH(N) \
struct __attribute__((packed)) packed##N {char pad[N];float value;}; \
unsigned narrow_packed##N(struct packed##N *p){union {__bf16 f;unsigned short u;} r;r.f=(__bf16)p->value;return r.u;} \
static struct packed##N storage##N[256];
PACKED_PATH(1)
PACKED_PATH(2)
PACKED_PATH(3)
static volatile float indirect_storage[256];
#endif
static unsigned failures,checks;
static void check(unsigned x,unsigned got,unsigned want){checks++;if(got!=want){failures++;if(failures<17){text("FAIL input=");hex(x);text(" got=");hex(got);text(" want=");hex(want);out('\n');}}}
int test_main(void){
    unsigned i,base,start,end,results[256];
    text(FORMAT_LABEL " BEGIN " PATH_NAME " part=");hex(PART);out('\n');
    if(PART==0) for(base=0;base<65536;base+=256){
        start=__ldtimer();
        for(i=0;i<256;i++)results[i]=widen(base+i);
        end=__ldtimer();
        for(i=0;i<256;i++)check(base+i,results[i],expected_widen(base+i));
        text("TIME widen ");hex(base);text(" " TIMING_UNIT "=");hex(end-start);out('\n');
    }
    for(base=0;base<VECTOR_COUNT;base+=256){
        unsigned count=VECTOR_COUNT-base;if(count>256)count=256;
        start=__ldtimer();
        for(i=0;i<count;i++)results[i]=narrow(VECTOR_INPUT(base+i));
        end=__ldtimer();
        for(i=0;i<count;i++)check(VECTOR_INPUT(base+i),results[i],VECTOR_WANT(base+i));
        text("TIME narrow ");hex(base);text(" " TIMING_UNIT "=");hex(end-start);out('\n');
#ifdef MEMORY_PATHS
        for(i=0;i<count;i++){
            union {float f;unsigned u;} x;x.u=VECTOR_INPUT(base+i);
            indirect_storage[i]=x.f;storage1[i].value=x.f;storage2[i].value=x.f;storage3[i].value=x.f;
        }
        {
            unsigned path;
            for(path=0;path<4;path++){
                start=__ldtimer();
                for(i=0;i<count;i++){
                    if(path==0)results[i]=narrow_indirect(&indirect_storage[i]);
                    else if(path==1)results[i]=narrow_packed1(&storage1[i]);
                    else if(path==2)results[i]=narrow_packed2(&storage2[i]);
                    else results[i]=narrow_packed3(&storage3[i]);
                }
                end=__ldtimer();
                for(i=0;i<count;i++)check(VECTOR_INPUT(base+i),results[i],VECTOR_WANT(base+i));
                text("TIME ");text(path==0?"indirect":path==1?"packed1":path==2?"packed2":"packed3");
                out(' ');hex(base);text(" " TIMING_UNIT "=");hex(end-start);out('\n');
            }
        }
#endif
    }
    text(failures?FORMAT_LABEL " FAIL checks=":FORMAT_LABEL " PASS checks=");hex(checks);text(" failed=");hex(failures);out('\n');
    return failures!=0;
}
