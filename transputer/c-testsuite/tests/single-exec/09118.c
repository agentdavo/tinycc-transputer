/* Regression: an indirect 64-bit operand held across evaluation of a shift
 * count must be staged in aligned words, even when preceding narrow locals
 * leave the frontend's local offset unaligned. Reduced from fuzz seed 329. */
int printf(const char *, ...);
typedef signed char i8; typedef unsigned char u8;
typedef short i16; typedef unsigned short u16;
typedef int i32; typedef unsigned u32;
typedef long long i64; typedef unsigned long long u64;
#define UMOD(W, a, b) ((W)(b) ? (W)(a) % (W)(b) : (W)(a))
#define SMOD32(a, b) (((i32)(b) == 0 || ((i32)(a) == (i32)0x80000000U && (i32)(b) == -1)) ? (i32)(a) : (i32)(a) % (i32)(b))
#define SDIV64(a, b) (((i64)(b) == 0 || ((i64)(a) == (i64)0x8000000000000000ULL && (i64)(b) == -1)) ? (i64)(a) : (i64)(a) / (i64)(b))
struct S { i8 a; u16 b; i32 c; u64 d; u8 e; };
struct B { unsigned u1 : 1; unsigned u3 : 3; unsigned u7 : 7; unsigned u13 : 13; int s5 : 5; unsigned u8x : 8; };
union U { u32 w; u16 h[2]; u8 b[4]; };
static u32 rec(u32 n, u32 acc) { return n ? rec(n - 1, acc * 31U + n) : acc; }
static u32 g0 = ((u32)0x80000000U);
static i64 g1 = ((i64)0x7fffffffffffffffULL);
static u16 g2 = ((u16)0x1U);
static u64 g3 = ((u64)0x0ULL);
static u64 g4 = ((u64)0x0ULL);
static u8 g5 = ((u8)0xfeU);
static u16 ga0[8] = { ((u16)0x7fffU), ((u16)0x1U), ((u16)0x1f41U), ((u16)0x8000U), ((u16)0x8000U), ((u16)0x8000U), ((u16)0xffffU), ((u16)0xcf42U) };
static u16 ga1[8] = { ((u16)0x1U), ((u16)0x5a78U), ((u16)0x1U), ((u16)0xffffU), ((u16)0x0U), ((u16)0xffffU), ((u16)0xdcebU), ((u16)0x6730U) };
static struct S g_s = { 15, 38190, 4951, 3364190043777095667ULL, 22 };
static struct B g_b;
static union U g_u = { 0x2e1a2d93U };
static i64 gm[4][5] = { { ((i64)0x7fffffffffffffffULL), ((i64)0x0ULL), ((i64)0x8000000000000000ULL), ((i64)0xf059fb685e71a3abULL), ((i64)0xffULL) }, { ((i64)0xffffffffffffffffULL), ((i64)0x6eec3642a7136547ULL), ((i64)0x80ULL), ((i64)0x4ULL), ((i64)0xe200272340d1edeeULL) }, { ((i64)0x1ULL), ((i64)0x0ULL), ((i64)0x485286bf43ba353eULL), ((i64)0x8000000000000000ULL), ((i64)0x8000000000000000ULL) }, { ((i64)0x1ULL), ((i64)0x0ULL), ((i64)0x2522865cefe887b0ULL), ((i64)0xffffffffffffffffULL), ((i64)0x80000000ULL) } };
static u32 h0(u32 x) { return (x * 0xe518fd4dU) ^ (x >> 1) ^ 0x2047ffceU; }
static u32 h1(u32 x) { return (x * 0xabbd53cfU) ^ (x >> 1) ^ 0x112aafafU; }
static u32 h2(u32 x) { return (x * 0xdbbcca6fU) ^ (x >> 30) ^ 0x35fe5fc4U; }
static u32 h3(u32 x) { return (x * 0x1230d325U) ^ (x >> 1) ^ 0x3d29e41eU; }
static u32 (*const fptab[4])(u32) = { h0, h1, h2, h3 };
static i32 f5(void)
{
    i32 l0 = ((i32)SMOD32(((i64)gm[((u32)0x3cd35539U) & 3][((u32)g1) % 5]), ((i32)((u64)((u32)gm[((u32)g2) & 3][((u32)g3) % 5]) | (u64)((i16)g_u.h[g0 & 1])))));
    u8 l1 = ((u8)((u32)((i32)((i16)((i64)l0) != (i16)((i16)g1))) << ((u32)((u8)((u32)((u32)ga1[(u32)((u32)g5) & 7]) + (u32)((i64)rec(((u32)0x7fffffffU) & 15, ((u32)g5))))) & 31)));
    i32 l2 = ((i32)SDIV64(((i16)0x0U), ((u32)((i32)((u16)g_s.b) >> ((u32)((i64)ga1[(u32)((u32)l0) & 7]) & 31)))));
    u16 la[8] = { ((u16)0x7fU), ((u16)0x8000U), ((u16)0xffffU), ((u16)0xffffU), ((u16)0xffffU), ((u16)0x1U), ((u16)0x5571U), ((u16)0xfffeU) };
    static u8 st = ((u8)0xa2U);
    { u16 *p = &la[(u32)((u32)l1) & 3]; p[1] = (u16)((u32)p[0] + (u32)((u16)((i16)((i32)l1) <= (i16)((u64)fptab[((u32)g3) & 3](((u32)l2)))))); *p ^= ((u16)UMOD(u64, ((u16)g_s.d), ((u64)g_b.u7))); }
    l0 = (i32)((u32)l0 + (u32)((i32)((i64)((u64)gm[((u32)g4) & 3][((u32)0xffU) % 5]) >> ((u32)((u64)(((i64)((u64)((u16)rec(((u32)st) & 15, ((u32)0xffffffffU))) > (u64)((u16)0xffffU))) || ((i8)ga0[(u32)((u32)g5) & 7]))) & 63))));
    return (i32)(l0);
}
int main(void) { printf("value %d\n", f5()); return 0; }
