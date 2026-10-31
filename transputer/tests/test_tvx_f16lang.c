/*
 * test_tvx_f16lang.c - __fp16 (IEEE-754 binary16) as a storage type.
 *
 * __fp16 follows the ARM ACLE rules: 2-byte storage, and every arithmetic
 * operand promotes to binary32, so an operation yields float and never
 * __fp16.  The standardised spelling _Float16 is deliberately NOT accepted
 * - see the comment in gen_op() in tccgen.c.
 *
 * Also checks that __fp16 and __bf16 are genuinely distinct: 1/3 rounds
 * differently in the two formats (10 vs 7 mantissa bits).
 */
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
static void ud(unsigned v){char b[12];int n=0;if(!v){up('0');return;}while(v){b[n++]='0'+v%10;v/=10;}while(n)up(b[--n]);}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}
static int bad=0,ck=0;
static void eq(const char*w,unsigned g,unsigned e){ck++;if(g!=e){bad++;us("\nFAIL ");us(w);us(" got=");uh(g);us(" want=");uh(e);}}
static __fp16 g_one = 1.0f;
static __fp16 g_arr[4] = { 1.0f, 2.0f, -0.5f, 65504.0f };  /* 65504 = f16 max */
static __fp16 dbl_h(__fp16 x){ return (__fp16)((float)x * 2.0f); }
struct S { int t; __fp16 h; __bf16 b; };
int test_main(void){
    __fp16 a,b; __bf16 c; float f; struct S s;
    eq("sizeof __fp16",  (unsigned)sizeof(__fp16),  2u);
    /* storage-only: an arithmetic operation yields float, not __fp16 */
    eq("promote to float", (unsigned)sizeof(a + b), 4u);
    eq("static 1.0",  f2b((float)g_one), 0x3F800000u);
    eq("arr0", f2b((float)g_arr[0]), 0x3F800000u);
    eq("arr2", f2b((float)g_arr[2]), 0xBF000000u);
    eq("arr3 max", f2b((float)g_arr[3]), f2b(65504.0f));
    a = 1.5f;
    eq("param/ret", f2b((float)dbl_h(a)), 0x40400000u);
    a = 1.0f; b = 0.5f;
    f = a + b;  eq("add", f2b(f), 0x3FC00000u);
    f = a * b;  eq("mul", f2b(f), 0x3F000000u);
    eq("cmp", (unsigned)(a > b), 1u);
    a = 1.0f; a += 0.5f; eq("+=", f2b((float)a), 0x3FC00000u);
    /* binary16 has more mantissa than bfloat16: 1/3 differs between them */
    f = 1.0f/3.0f;
    a = (__fp16)f;  c = (__bf16)f;
    eq("f16 RNE",  f2b((float)a), __f16tof32(__f32tof16(f2b(f))));
    eq("bf16 RNE", f2b((float)c), __bf16tof32(__f32tobf16(f2b(f))));
    us("\n f16(1/3)="); uh(f2b((float)a)); us(" bf16(1/3)="); uh(f2b((float)c));
    ck++; if(f2b((float)a)==f2b((float)c)){bad++;us(" [SHOULD DIFFER]");}
    /* both narrow types coexist in one struct */
    s.t=3; s.h=2.0f; s.b=4.0f;
    eq("struct h", f2b((float)s.h), 0x40000000u);
    eq("struct b", f2b((float)s.b), 0x40800000u);
    /* f16 overflow -> Inf */
    a = (__fp16)70000.0f;
    eq("overflow->inf", f2b((float)a), 0x7F800000u);
#if defined(__FP16__) && defined(__BF16__) && defined(__FLOAT16__)
    ck++;
#else
    bad++; us("\nFAIL feature macros");
#endif
    /* ---- _Float16: C23 Annex H arithmetic type (contrast with __fp16) ---- */
    {
        _Float16 x = 1.0f, y = 0.5f; float wf = 2.0f;
        eq("sizeof _Float16", (unsigned)sizeof(_Float16), 2u);
        /* an operation is performed in the type and yields the type */
        eq("f16 op f16 -> f16", (unsigned)sizeof(x + y), 2u);
        /* ... unlike __fp16, which always yields float */
        eq("fp16 op fp16 -> float", (unsigned)sizeof(a + b), 4u);
        /* usual arithmetic conversions: a wider real type still wins */
        eq("f16 op float -> float", (unsigned)sizeof(x + wf), 4u);
        /* integers convert to _Float16, so the result stays _Float16 */
        eq("f16 op int -> f16", (unsigned)sizeof(x + 1), 2u);
        /* a relational operation yields int */
        eq("f16 cmp -> int", (unsigned)sizeof(x > y), 4u);
        /* values still compute correctly through the float emulation */
        eq("f16 add", f2b((float)(_Float16)(x + y)), 0x3FC00000u);
        eq("f16 mul", f2b((float)(_Float16)(x * y)), 0x3F000000u);
        eq("f16 cmp val", (unsigned)(x > y), 1u);
        x = 1.0f; x += 0.5f;
        eq("f16 +=", f2b((float)x), 0x3FC00000u);
        /* strict per-operation rounding: 1 + 2^-11 is not representable in
           binary16, so it rounds to 1.0 at the point of the addition and the
           following add cannot recover it.  Excess-precision emulation that
           kept the intermediate in binary32 would give a different answer. */
        {
            _Float16 e = (_Float16)0.00048828125f; /* 2^-11 */
            _Float16 t = (_Float16)1.0f;
            t = t + e;
            eq("strict rounding", f2b((float)t), 0x3F800000u);
        }
        /* same format as __fp16: identical storage for the same value */
        {
            _Float16 u1 = (_Float16)(1.0f/3.0f);
            __fp16   u2 = (__fp16)(1.0f/3.0f);
            eq("f16 == fp16 format", f2b((float)u1), f2b((float)u2));
        }
    }
    us("\nchecks="); ud((unsigned)ck); us(" failures="); ud((unsigned)bad);
    us(bad?"\nF16LANG: FAIL\n":"\nF16LANG: PASS\n");
    return bad;
}
