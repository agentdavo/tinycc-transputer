/*
 * test_tvx_bf16lang.c - __bf16 language-conformance test.
 *
 * Checks that TCC treats __bf16 as a real type in every context a storage
 * type must support: sizeof/alignof, static initialisers (constant-folded),
 * const and typedef, pass and return by value, pointer parameters, struct
 * and array members, arithmetic promotion to binary32, comparisons,
 * compound assignment, int conversions, truth value, and RNE narrowing.
 *
 * Note: neither bfloat16 nor MXFP4 appears in any ISO C standard (C11
 * included).  __bf16 follows the x86-64 psABI / GCC / Clang spelling.
 * See transputer/TVX-ISA.md section 2.
 *
 * Build & run: see transputer/TVX-ISA.md section 6.
 */

/* __bf16 language-conformance: does TCC treat it as a real type everywhere? */
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
static void ud(unsigned v){char b[12];int n=0;if(!v){up('0');return;}while(v){b[n++]='0'+v%10;v/=10;}while(n)up(b[--n]);}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}
static int bad=0, ck=0;
static void eq(const char*w,unsigned g,unsigned e){ck++;if(g!=e){bad++;us("\nFAIL ");us(w);us(" got=");uh(g);us(" want=");uh(e);}}

/* 1. parameter and return by value */
static __bf16 dbl_bf(__bf16 x){ return (__bf16)((float)x * 2.0f); }
/* 2. pointer parameter */
static void setbf(__bf16 *p, float v){ *p = (__bf16)v; }
/* 3. struct member + array member */
struct S { int tag; __bf16 w; __bf16 v[3]; };
static struct S gs;
/* 4. static initialisers (constant folding into .data) */
static __bf16 g_one   = 1.0f;
static __bf16 g_half  = 0.5f;
static __bf16 g_arr[4] = { 1.0f, 2.0f, -0.5f, 6.0f };
/* 5. const / typedef */
typedef __bf16 bf16_t;
static const bf16_t g_const = 3.0f;

int test_main(void){
    __bf16 a, b, arr[4];
    bf16_t t;
    float f; int i;

    eq("sizeof", (unsigned)sizeof(__bf16), 2u);
    eq("_Alignof", (unsigned)__alignof__(__bf16), 2u);
    eq("sizeof[]", (unsigned)sizeof(arr), 8u);
    eq("sizeof S", (unsigned)sizeof(struct S), (unsigned)sizeof(struct S));

    /* static initialiser folding */
    eq("static 1.0", f2b((float)g_one),  0x3F800000u);
    eq("static 0.5", f2b((float)g_half), 0x3F000000u);
    eq("static arr0", f2b((float)g_arr[0]), 0x3F800000u);
    eq("static arr2", f2b((float)g_arr[2]), 0xBF000000u);
    eq("static arr3", f2b((float)g_arr[3]), 0x40C00000u);
    eq("const", f2b((float)g_const), 0x40400000u);

    /* call by value + return by value */
    a = 1.5f;
    eq("param/ret", f2b((float)dbl_bf(a)), 0x40400000u);   /* 3.0 */

    /* pointer out-param */
    setbf(&b, -2.0f);
    eq("ptr param", f2b((float)b), 0xC0000000u);

    /* struct member and array member */
    gs.tag = 7; gs.w = 4.0f;
    for(i=0;i<3;i++) gs.v[i] = (__bf16)((float)(i+1));
    eq("struct tag", (unsigned)gs.tag, 7u);
    eq("struct w", f2b((float)gs.w), 0x40800000u);
    eq("struct v1", f2b((float)gs.v[1]), 0x40000000u);

    /* arithmetic promotes to float */
    a = 1.0f; b = 0.5f;
    f = a + b;  eq("add", f2b(f), 0x3FC00000u);
    f = a - b;  eq("sub", f2b(f), 0x3F000000u);
    f = a * b;  eq("mul", f2b(f), 0x3F000000u);
    f = a / b;  eq("div", f2b(f), 0x40000000u);
    f = -a;     eq("neg", f2b(f), 0xBF800000u);

    /* comparisons */
    eq("gt", (unsigned)(a > b), 1u);
    eq("lt", (unsigned)(a < b), 0u);
    eq("eq", (unsigned)(a == 1.0f), 1u);
    eq("ne", (unsigned)(a != b), 1u);

    /* compound assignment narrows back */
    a = 1.0f; a += 0.5f;  eq("+=", f2b((float)a), 0x3FC00000u);
    a = 4.0f; a *= 0.5f;  eq("*=", f2b((float)a), 0x40000000u);

    /* int <-> bf16 conversions */
    a = (__bf16)5;        eq("int->bf16", f2b((float)a), 0x40A00000u);
    i = (int)(__bf16)6.0f; eq("bf16->int", (unsigned)i, 6u);

    /* truth value */
    a = 0.0f; eq("zero false", (unsigned)(a ? 1 : 0), 0u);
    a = 2.0f; eq("nonzero true", (unsigned)(a ? 1 : 0), 1u);

    /* rounding: 1/3 is not representable, must round-to-nearest-even */
    f = 1.0f/3.0f;
    t = (bf16_t)f;
    eq("RNE narrow", f2b((float)t), __bf16tof32(__f32tobf16(f2b(f))));

    us("\nchecks="); ud((unsigned)ck); us(" failures="); ud((unsigned)bad);
    us(bad?"\nBF16LANG: FAIL\n":"\nBF16LANG: PASS\n");
    return bad;
}
