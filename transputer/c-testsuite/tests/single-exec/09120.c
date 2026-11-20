/* Regression: as 09119 (a jump target on padding ended no range window),
 * here on an i8/u16 mix in a ?: chain.  Reduced from fuzz seed 43018. */
#include <stdarg.h>
int printf(const char *, ...);
typedef signed char i8; typedef unsigned char u8;
typedef short i16; typedef unsigned short u16;
typedef int i32; typedef unsigned u32;
typedef long long i64; typedef unsigned long long u64;
typedef float f32; typedef double f64;
#define SDIV64(a, b) (((i64)(b) == 0 || ((i64)(a) == (i64)0x8000000000000000ULL && (i64)(b) == -1)) ? (i64)(a) : (i64)(a) / (i64)(b))
#define F2I(x) (((x) > -2147483648.0 && (x) < 2147483648.0) ? (i32)(x) : (i32)0)
struct S { i8 a; u16 b; i32 c; u64 d; u8 e; };
struct B { unsigned u1 : 1; unsigned u3 : 3; unsigned u7 : 7; unsigned u13 : 13; int s5 : 5; unsigned u8x : 8; };
static u64 crc = 0x6a09e667f3bcc908ULL;
static void mix(u64 v) { crc = (crc ^ v) * 0x100000001b3ULL; crc ^= crc >> 29; }
static void show(const char *tag) { printf("%s %08x%08x\n", tag, (u32)(crc >> 32), (u32)crc); }
static u64 vsum(int n, ...)
{
    va_list ap; u64 s = 0; int i;
    return s;
}
static struct S mk_s(u32 x, u64 y)
{
    struct S s;
    return s;
}
union U { u32 w; u16 h[2]; u8 b[4]; };
static u32 rec(u32 n, u32 acc) { return n ? rec(n - 1, acc * 31U + n) : acc; }
static f64 vsumd(int n, ...)
{
    va_list ap; f64 s = 0; int i;
    return s;
}
static i8 g0 = ((i8)0x52U);
static u8 g1 = ((u8)0xffU);
static u16 g2 = ((u16)0x7fffU);
static i32 g3 = ((i32)0x0U);
static i8 g4 = ((i8)0x7fU);
static i8 g5 = ((i8)0x1U);
static i8 g6 = ((i8)0x7U);
static u8 g7 = ((u8)0xffU);
static i64 ga0[8] = { ((i64)0x7fffffffffffffffULL), ((i64)0x6eb12ace2bc8bcaaULL), ((i64)0x7ULL), ((i64)0x7fffULL), ((i64)0xf1e2c2444c75d46cULL), ((i64)0x364fc9855b504206ULL), ((i64)0x7fffffffffffffffULL), ((i64)0x7fffffffffffffffULL) };
static struct S g_s = { 102, 48007, 1431582044, 8379872340910741362ULL, 128 };
static struct B g_b;
static union U g_u = { 0x5f772efU };
static u32 gm[4][5] = { { ((u32)0x86445ab9U), ((u32)0x88050a4bU), ((u32)0x1U), ((u32)0x1U), ((u32)0x1U) }, { ((u32)0x5f35b9e0U), ((u32)0x7fffffffU), ((u32)0x46d5d89U), ((u32)0xffffffffU), ((u32)0xffU) }, { ((u32)0xa93a39c2U), ((u32)0x7fffffffU), ((u32)0xf25b2a9dU), ((u32)0xffffffffU), ((u32)0x0U) }, { ((u32)0x7fffffffU), ((u32)0xba4fd5abU), ((u32)0x0U), ((u32)0xc5af8e53U), ((u32)0x0U) } };
static u32 h0(u32 x) { return (x * 0x1d2de7f9U) ^ (x >> 1) ^ 0x2bfc04f0U; }
static u32 h1(u32 x) { return (x * 0x920c8255U) ^ (x >> 20) ^ 0xa419ab84U; }
static u32 h2(u32 x) { return (x * 0xef2c2287U) ^ (x >> 10) ^ 0x48eea7aeU; }
static u32 h3(u32 x) { return (x * 0xb9c58b5dU) ^ (x >> 27) ^ 0xa39b2aaeU; }
static u32 (*const fptab[4])(u32) = { h0, h1, h2, h3 };
static u16 f0(i64 p0, u8 p1, u8 p2, u16 p3)
{
    int k0, k1, w0, w1;
    i16 l0 = ((i16)((i32)((u32)(((i64)g_u.b[((u32)g2) & 3]) ? ((u32)g_u.w) : ((u32)0x0U))) >> ((u32)((u64)gm[((u32)p1) & 3][((u32)p2) % 5]) & 31)));
    u32 l1 = ((u32)(((i32)((i64)((i16)0x0U) >> ((u32)((i8)0x0U) & 63))) ? ((u32)((i32)((u16)g_s.d) < (i32)((u64)fptab[((u32)g2) & 3](((u32)0xffffffffU))))) : ((u32)(((u8)ga0[(u32)((u32)l0) & 7]) ? ((u32)0xeb469354U) : ((u32)0xc50a90bdU)))));
    u16 l2 = ((u16)ga0[(u32)l1 & 7]);
    u8 l3 = ((u8)g_b.u1);
    static u64 st = ((u64)0xe3fa38a727b531cULL);
    for (k0 = 0; k0 < 2; k0++) {
    }
    return (u16)((u64)l0 ^ (u64)l1 ^ (u64)l2 ^ (u64)l3 ^ (u64)st ^ (u64)((u16)((u32)((u32)g_u.h[((u32)0xfffffff9U) & 1]) - (u32)((u64)g_u.b[((u32)g2) & 3]))));
}
static i32 f1(i8 p0, i16 p1, u8 p2, u32 p3, u32 p4, u32 p5, u16 p6)
{
    u16 l0 = ((u16)(((u16)((u32)((i16)0x1U) & (u32)((u8)0xaeU))) && ((i16)F2I(((f32)((u16)0x0U))))));
    i32 l1 = ((i32)0x34864fd1U);
    u64 la[8] = { ((u64)0x0ULL), ((u64)0xeb568e74b42c8138ULL), ((u64)0x8000000000000000ULL), ((u64)0x7fULL), ((u64)0x718782d8ccb27610ULL), ((u64)0x1ULL), ((u64)0xfffffffffffffffeULL), ((u64)0x77df50a457515cffULL) };
    return (u32)((u64)l0 ^ (u64)l1 ^ (u64)((u32)0x2272b1e8U) ^ (u64)la[(u32)((u32)l0) & 7]);
}
static u32 f4(u8 p0)
{
    u16 l0 = ((u16)0x1362U);
    i16 l1 = ((i16)((u32)((i64)((u64)((i32)fptab[((u32)g2) & 3](((u32)g5))) >> ((u32)((i16)g1) & 63))) + (u32)((i64)((u64)((u32)gm[((u32)g0) & 3][((u32)0x0U) % 5]) << ((u32)((u32)ga0[(u32)((u32)g3) & 7]) & 63)))));
    return (u32)((u64)l0 ^ (u64)l1 ^ (u64)((u32)((i64)((i64)((u64)((u16)fptab[((u32)g4) & 3](((u32)0xffffffffU))) << ((u32)((u64)g_b.u13) & 63))) != (i64)((u64)((u64)((u32)0xfffffffaU) + (u64)((i64)fptab[((u32)g7) & 3](((u32)0x80U))))))));
}
static struct S f5(i16 p0, u8 p1, struct S p2)
{
    {
        struct S rs;
        return rs;
    }
}
static i64 f7(i8 p0, u32 p1)
{
    i32 l0 = ((i32)rec(p1 & 15, ((u32)g2)));
    u8 l1 = ((u8)ga0[(u32)((u32)0x1U) & 7]);
    i16 l2 = ((i16)((u64)((u8)g_s.b) - (u64)((u32)((u32)((i32)ga0[(u32)((u32)g5) & 7]) ^ (u32)((i8)0xffU)))));
    u64 la[8] = { ((u64)0x7fffffffffffffffULL), ((u64)0x1ULL), ((u64)0x0ULL), ((u64)0xffffffffffffffffULL), ((u64)0x1ULL), ((u64)0x2e8651cc4dfcb34fULL), ((u64)0x1ULL), ((u64)0x7fffffffffffffffULL) };
    if (((i32)fptab[((u32)l2) & 3](((u32)p0)))) {
    } else {
        l0 = ((i32)(((u64)F2I(((f32)((u8)0x44U)))) && ((u16)ga0[(u32)((u32)0x7fffffffU) & 7])));
    }
    la[(u32)((u32)l0) & 7]--;
    return (i64)((u64)l0 ^ (u64)l1 ^ (u64)l2 ^ (u64)((i64)(((u8)((u64)((i64)ga0[(u32)p1 & 7]) & (u64)((u64)g_s.a))) || ((u32)g5))) ^ (u64)la[(u32)((u32)g4) & 7]);
}
static u16 f8(i32 p0, struct S p1)
{
    i64 l0 = ((i64)((u64)((i32)((u32)((i8)fptab[((u32)g4) & 3](((u32)g1))) + (u32)((u64)0x7fffULL))) + ((u64)f1(((i8)(((i64)0x1ULL) ? ((i8)gm[((u32)0xffffffffU) & 3][((u32)0x80000000U) % 5]) : ((i8)g_u.b[((u32)g5) & 3]))), ((i16)(((i32)gm[((u32)0xffffffffU) & 3][((u32)0xffffffffU) % 5]) && ((u8)gm[((u32)0x80000000U) & 3][((u32)0x0U) % 5]))), ((u8)gm[((u32)0x80U) & 3][((u32)g2) % 5]), ((u32)(((i64)g2) ? ((u32)g_u.b[((u32)0x2937eb74U) & 3]) : ((u32)rec(((u32)g7) & 15, ((u32)g3))))), ((u32)g4), ((u32)g2), ((u16)fptab[((u32)0xffffffffU) & 3](((u32)g6)))) * (u64)((i8)((i64)((i16)ga0[(u32)((u32)g6) & 7]) <= (i64)((i32)g_u.h[((u32)g5) & 1]))))));
    i16 l1 = ((i16)((u32)((u8)(((i16)g_b.u7) ? ((u8)g6) : ((u8)rec(((u32)p0) & 15, ((u32)0x80000000U))))) >> ((u32)((u16)((u32)((i16)0xffffU) - (u32)((i64)fptab[((u32)g3) & 3](((u32)g5))))) & 31)));
    u16 la[8] = { ((u16)0x0U), ((u16)0x295eU), ((u16)0x7fffU), ((u16)0x3a0eU), ((u16)0x8000U), ((u16)0x7fffU), ((u16)0x6U), ((u16)0xffU) };
    switch ((u32)((i8)((u32)((i16)((u64)((i16)0x0U) + (u64)((i16)0x8000U))) >= (u32)((i64)((i8)((u8)g0) < (i8)((u64)g_u.b[((u32)g7) & 3]))))) & 4095) {
    }
    return (u16)((u64)l0 ^ (u64)l1 ^ (u64)((u16)0xd59aU) ^ (u64)la[(u32)((u32)0xffffffffU) & 7]);
}
static u8 f9(void)
{
    int k0, k1, w0, w1;
    u16 l0 = ((u16)((u32)((u64)F2I(((f32)6.5e3))) - (u32)((u16)g_b.u3)));
    i16 l1 = ((i16)((u32)((u64)0x39565ff54261a4dULL) - (u32)((u16)((u32)((i16)0xffffU) * (u32)((i8)g_u.w)))));
    u8 l2 = ((u8)f7(((i8)(i16)g5), ((u32)SDIV64(((u8)g_s.c), ((u32)fptab[((u32)g2) & 3](((u32)g5)))))));
    u16 la[8] = { ((u16)0x7fffU), ((u16)0xffffU), ((u16)0x4aacU), ((u16)0xffffU), ((u16)0x0U), ((u16)0x8000U), ((u16)0x8U), ((u16)0xffffU) };
    static u16 st = ((u16)0xfffbU);
    for (k0 = 0; k0 < 1; k0++) {
    }
    return (u8)((u64)l0 ^ (u64)l1 ^ (u64)l2 ^ (u64)st ^ (u64)((u8)((i32)((u16)ga0[(u32)((u32)0x80000000U) & 7]) >> ((u32)((i16)g_s.c) & 31))) ^ (u64)la[(u32)((u32)0x61fd6722U) & 7]);
}
int main(void)
{
    mix((u64)f7(((i8)(((u64)0x0ULL) || ((i16)gm[((u32)g7) & 3][((u32)0x7U) % 5]))), ((u32)(((i8)0xffU) && ((u8)g_b.u8x)))));
    show("f7");
}
