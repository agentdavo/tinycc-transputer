/* Runtime/alias/packed regressions for workspace-backed TVX narrowing. */
#ifdef BINARY16
typedef _Float16 narrow_t;
#define ONE_HALF 0x3e00u
#define TWO_HALF 0x4100u
#define THREE_QUARTERS 0x4380u
#else
typedef __bf16 narrow_t;
#define ONE_HALF 0x3fc0u
#define TWO_HALF 0x4020u
#define THREE_QUARTERS 0x4070u
#endif
static volatile float source=1.5f;
static unsigned bad, checks;
static void text(const char *s){while(*s){while(!(*(volatile unsigned*)0x10000004u&1)){} *(volatile unsigned*)0x10000000u=*s++;}}
static unsigned bits(narrow_t v){union {narrow_t f;unsigned short u;} x;x.f=v;return x.u;}
static unsigned narrow_value(float v){return bits((narrow_t)v);}
static float returned(float v){return v;}
static unsigned indirect(volatile float *p){return bits((narrow_t)*p);}
struct __attribute__((packed)) packed_float { char pad; float value; };
static unsigned packed_pointer(struct packed_float *p){return bits((narrow_t)p->value);}
static void assign(volatile float *p,float v){*p=v;}
static void check(unsigned got,unsigned want){checks++;if(got!=want){bad++;text("NARROW_CONTEXT mismatch\n");}}
int test_main(void){
    float local=source;
    volatile float changing=local;
    struct packed_float p;
    p.pad=7;p.value=local;
    check(narrow_value(local),ONE_HALF);
    check(indirect(&changing),ONE_HALF);
    assign(&changing,2.5f);
    check(indirect(&changing),TWO_HALF);
    check(bits((narrow_t)changing),TWO_HALF);
    check(packed_pointer(&p),ONE_HALF);
    p.value=returned(2.5f);
    check(packed_pointer(&p),TWO_HALF);
    check(bits((narrow_t)p.value),TWO_HALF);
    check(narrow_value(returned(local)),ONE_HALF);
    check(narrow_value(returned(local)+2.25f),THREE_QUARTERS);
    check((narrow_value(returned(local))<<16)|narrow_value(returned(2.5f)),(ONE_HALF<<16)|TWO_HALF);
    check(bits((narrow_t)-0.0f),0x8000);
    check(bits((narrow_t)0.0f),0);
    if(checks!=12)bad++;
    text(bad?"NARROW_CONTEXT: FAIL\n":"NARROW_CONTEXT: PASS checks=12\n");
    return bad!=0;
}
