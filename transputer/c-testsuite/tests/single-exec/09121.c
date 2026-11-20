/* Regression: gen_opi's direct operand pushes (local op local, local op
 * const, const op local) took the result register after pushing the two
 * operands; a get_reg() that had to spill a halfword register then ran
 * `ldl r; ldlp home; ss` on top of them and pushed the first operand out
 * of CReg (`p0 & 3` lost p0).  Reduced from fuzz seed 48076. */
#include <stdarg.h>
int printf(const char *, ...);
typedef signed char i8; typedef unsigned char u8;
typedef short i16; typedef unsigned short u16;
typedef int i32; typedef unsigned u32;
typedef long long i64; typedef unsigned long long u64;
typedef float f32; typedef double f64;
#define UMOD(W, a, b) ((W)(b) ? (W)(a) % (W)(b) : (W)(a))
#define SMOD32(a, b) (((i32)(b) == 0 || ((i32)(a) == (i32)0x80000000U && (i32)(b) == -1)) ? (i32)(a) : (i32)(a) % (i32)(b))
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
static void mix_s(struct S s) { mix((u64)(i64)s.a); mix(s.b); mix((u64)(i64)s.c); mix(s.d); mix(s.e); }
union U { u32 w; u16 h[2]; u8 b[4]; };
static u32 rec(u32 n, u32 acc) { return n ? rec(n - 1, acc * 31U + n) : acc; }
static f64 vsumd(int n, ...)
{
    va_list ap; f64 s = 0; int i;
    va_start(ap, n);
    for (i = 0; i < n; i++)
        s = s * 0.5 + va_arg(ap, f64);
    return s;
}
static u16 g0 = ((u16)0x525cU);
static i16 g1 = ((i16)0x884U);
static u8 g2 = ((u8)0xfbU);
static i16 g3 = ((i16)0x7fffU);
static i8 ga0[8] = { ((i8)0xddU), ((i8)0x80U), ((i8)0xffU), ((i8)0x1U), ((i8)0xc0U), ((i8)0x7bU), ((i8)0x1U), ((i8)0x7fU) };
static struct S g_s = { -97, 56631, 559760034, 13475894084549079906ULL, 103 };
static struct B g_b;
static union U g_u = { 0x37242a48U };
static i32 gm[4][5] = { { ((i32)0xffffffffU), ((i32)0x0U), ((i32)0xbd40a42cU), ((i32)0x80000000U), ((i32)0x0U) }, { ((i32)0x1U), ((i32)0xffffffffU), ((i32)0x5U), ((i32)0x7fffffffU), ((i32)0xffffffffU) }, { ((i32)0x1U), ((i32)0x80000000U), ((i32)0x80000000U), ((i32)0x876dcb6eU), ((i32)0xffffffffU) }, { ((i32)0x21a43b0eU), ((i32)0x0U), ((i32)0x90946717U), ((i32)0xfffffffeU), ((i32)0xc5793c49U) } };
static u32 h0(u32 x) { return (x * 0x7fe1c83U) ^ (x >> 10) ^ 0x4c9a0acU; }
static u32 h1(u32 x) { return (x * 0x2ed85bf5U) ^ (x >> 23) ^ 0xa81e5ac7U; }
static u32 h2(u32 x) { return (x * 0x26c490b9U) ^ (x >> 8) ^ 0xa3721fbeU; }
static u32 h3(u32 x) { return (x * 0xeefbe85fU) ^ (x >> 27) ^ 0x9ebc607cU; }
static u32 (*const fptab[4])(u32) = { h0, h1, h2, h3 };
static u64 f0(i32 p0, i64 p1, i8 p2, i16 p3, u32 p4, i16 p5, i16 p6)
{
    i64 l0 = ((i64)((u64)((i32)(((i8)g0) || ((i8)g2))) << ((u32)((u16)((i64)((i32)g_s.d) >> ((u32)((i32)p2) & 63))) & 63)));
    i64 l1 = ((i64)((u64)((u64)((i8)((i16)p2) <= (i8)((i32)g_b.u13))) << ((u32)((u64)g_b.u1) & 63)));
    i8 la[8] = { ((i8)0x9U), ((i8)0x3fU), ((i8)0x7fU), ((i8)0x6aU), ((i8)0x86U), ((i8)0xffU), ((i8)0x86U), ((i8)0x1U) };
    l0 = (i64)F2I(vsumd(2, (f64)((f64)(((f64)((u16)gm[((u32)0x0U) & 3][((u32)p0) % 5])) + ((f64)((i8)g0)))), (f32)((f32)((f32)((u16)0xffffU)))));
    l0 &= ((i64)((u64)((u32)((u32)((u8)((u32)((u64)p2) * (u32)((u8)0x7fU))) - (u32)((i32)(((i8)la[(u32)((u32)g1) & 7]) ? ((i32)rec(((u32)0xed7e7fecU) & 15, ((u32)p1))) : ((i32)g_u.h[((u32)g1) & 1]))))) >> ((u32)((i64)((u64)((i64)(((u32)fptab[((u32)g2) & 3](((u32)p2))) || ((i32)fptab[((u32)p2) & 3](((u32)p1))))) * (u64)((u32)rec(((u32)l0) & 15, ((u32)l1))))) & 63)));
    return (u64)((u64)l0 ^ (u64)l1 ^ (u64)((u64)((u64)((i64)gm[((u32)g0) & 3][p4 % 5]) + (u64)((i64)0x7fffffffULL))) ^ (u64)la[(u32)((u32)p5) & 7]);
}
static struct S f1(i16 p0, i64 p1, u64 p2, i32 p3)
{
    i32 l0 = ((i32)F2I(((f32)((f32)((i8)g_u.h[((u32)0xe48382e4U) & 1])))));
    u16 la[8] = { ((u16)0x973cU), ((u16)0x12c9U), ((u16)0x775dU), ((u16)0x8000U), ((u16)0xffffU), ((u16)0x7fffU), ((u16)0x8000U), ((u16)0x7fffU) };
    static u16 st = ((u16)0x7U);
    g_b.u13 = (u32)((u32)g3);
    {
        struct S rs;
        rs.a = ((i8)((i32)l0 >> ((u32)((u32)((u32)((i8)gm[((u32)0x72ce51eeU) & 3][((u32)p0) % 5]) | (u32)((u16)fptab[((u32)0xffffU) & 3](((u32)0x1U))))) & 31)));
        rs.b = g0;
        rs.d = ((u64)((u64)((u8)(((i8)0x1U) || ((i32)g1))) & (u64)((i16)((u32)((u32)gm[((u32)st) & 3][((u32)0x0U) % 5]) - (u32)((i64)0xce6a321dce4c5105ULL)))));
        rs.e = ((u8)(((i8)((u32)((u8)g_s.e) * (u32)((u8)la[(u32)((u32)0xffffffffU) & 7]))) ? ((u8)((i64)((u64)st) >> ((u32)((i16)0x7fffU) & 63))) : ((u8)((u32)((u8)0xf5U) >> ((u32)((u8)0x0U) & 31)))));
        return rs;
    }
}
static i16 f2(u32 p0, i32 p1)
{
    int k0, k1, w0, w1;
    u16 l0 = ((u16)((u64)((u16)((u64)((i32)g_u.h[((u32)0x7U) & 1]) & (u64)((i32)g_s.a))) - (u64)((u64)((u64)((i8)0x7fU) + (u64)((i16)0xb126U)))));
    i64 l1 = ((i64)((u64)((i32)fptab[p0 & 3](p0)) + (u64)((i64)((u64)((u8)g_b.u1) * (u64)((u64)g3)))));
    i64 la[8] = { ((i64)0x1ULL), ((i64)0xffffffffffffffffULL), ((i64)0x7fffffffULL), ((i64)0xffffffffffffffffULL), ((i64)0x7fffffffffffffffULL), ((i64)0x8000000000000000ULL), ((i64)0x0ULL), ((i64)0x667ac84e0b2989e5ULL) };
    for (k0 = 0; k0 < 3; k0++) {
        l1 = ((i64)f0(((i32)rec(((u32)g3) & 15, ((u32)0x7fU))), ((i64)((u64)((i16)gm[((u32)0x7U) & 3][((u32)0x1U) % 5]) + ((u64)f0(((i32)0x0U), ((i64)(((u64)g_s.e) ? ((i64)gm[((u32)0xffffffffU) & 3][p0 % 5]) : ((i64)g_u.h[((u32)0x0U) & 1]))), ((i8)((u64)((u16)0xc2abU) | (u64)((i16)g_s.a))), ((i16)g_u.h[((u32)0xfffffffdU) & 1]), ((u32)g3), ((i16)gm[((u32)0x1U) & 3][((u32)0x7fffffffU) % 5]), ((i16)F2I(((f64)((i16)gm[((u32)g2) & 3][((u32)0xbe1ee476U) % 5]))))) * (u64)((i16)0x8000U)))), ((i8)fptab[((u32)0x7fffffffU) & 3](((u32)l0))), ((i16)F2I(((f32)((i8)g_s.d)))), ((u32)((u64)((i64)g_u.b[((u32)g2) & 3]) ^ ((u64)f0(((i32)g_b.u13), ((i64)((u64)((i32)g1) * (u64)((u32)gm[p0 & 3][((u32)g1) % 5]))), ((i8)((u32)((u32)0x80000000U) * (u32)((u64)g1))), ((i16)rec(((u32)l0) & 15, ((u32)l1))), ((u32)(((i32)ga0[(u32)((u32)g3) & 7]) ? ((u32)l1) : ((u32)g1))), ((i16)((u16)((i32)g_u.h[((u32)l1) & 1]) > (u16)((i64)g0))), ((i16)l0)) * (u64)((u64)l0)))), ((i16)((u32)g0 + (u32)((u8)gm[((u32)l0) & 3][((u32)0x0U) % 5]))), ((i16)(u16)((i32)g_s.d))));
    }
    return (i16)((u64)l0 ^ (u64)l1 ^ (u64)((i16)(((u16)SMOD32(((u32)gm[((u32)g0) & 3][((u32)l1) % 5]), ((u64)fptab[((u32)0x7fU) & 3](((u32)g2))))) && ((u64)gm[((u32)l0) & 3][((u32)p1) % 5]))) ^ (u64)la[(u32)((u32)l0) & 7]);
}
static i16 f3(i8 p0, u8 p1, u64 p2)
{
    u16 l0 = ((u16)((u32)((i16)(((i16)rec(((u32)g1) & 15, ((u32)g2))) || ((u16)g2))) * (u32)((i64)((u16)((i32)fptab[((u32)g0) & 3](((u32)p1))) == (u16)((u64)0x0ULL)))));
    u64 l1 = ((u64)((u64)((u64)(~(u64)((i16)0x7fffU))) | (u64)((u32)((u32)((u64)fptab[((u32)g2) & 3](((u32)g3))) - (u32)((i32)g1)))));
    u8 l2 = ((u8)0x1fU);
    u64 l3 = ((u64)g_b.u3);
    u32 la[8] = { ((u32)0x7fffffffU), ((u32)0x9979c6caU), ((u32)0xf5646813U), ((u32)0xedc45fa5U), ((u32)0x565d952aU), ((u32)0x87c62dc1U), ((u32)0x8000U), ((u32)0x6c79f9faU) };
    return (i16)((u64)l0 ^ (u64)l1 ^ (u64)l2 ^ (u64)l3 ^ (u64)((i16)(((i32)(u16)((u64)ga0[(u32)((u32)p0) & 7])) ? ((i16)0xf21eU) : ((i16)((i32)((i16)l0) >> ((u32)((u8)g_u.w) & 31))))) ^ (u64)la[(u32)((u32)l3) & 7]);
}
static i32 f4(i8 p0, u8 p1, i8 p2)
{
    i8 l0 = ((i8)((u64)((u32)0x0U) > (u64)((u64)((u64)((i64)g2) >> ((u32)((i8)g_u.w) & 63)))));
    u16 l1 = ((u16)fptab[((u32)l0) & 3](((u32)0x2U)));
    return (i32)((u64)l0 ^ (u64)l1 ^ (u64)((i32)0xffffffffU));
}
static i8 f5(i64 p0, i64 p1, struct S p2, u32 p3, struct S p4, u8 p5, i32 p6)
{
    u16 l0 = ((u16)g_s.a);
    u64 l1 = ((u64)0x3c68a0b09ddc55bcULL);
    i32 l2 = ((i32)0x4U);
    u32 l3 = ((u32)(~(u32)((i64)rec(((u32)0x5U) & 15, ((u32)l0)))));
    i8 la[8] = { ((i8)0xffU), ((i8)0x80U), ((i8)0x4U), ((i8)0x80U), ((i8)0x0U), ((i8)0x7fU), ((i8)0x7fU), ((i8)0x80U) };
    if (((i16)(!(u32)((u8)((u32)((i8)la[(u32)((u32)p6) & 7]) + (u32)((u64)0xffffffffffffffffULL)))))) {
    }
    return (i8)((u64)l0 ^ (u64)l1 ^ (u64)l2 ^ (u64)l3 ^ (u64)((i8)UMOD(u32, ((i8)((u32)((i8)rec(((u32)0x80000000U) & 15, ((u32)p5))) + (u32)((u8)g_b.u8x))), ((u64)((u64)((i8)0x80U) << ((u32)((i32)g_u.h[((u32)p5) & 1]) & 63))))) ^ (u64)la[(u32)((u32)g3) & 7]);
}
static i8 f6(i16 p0, struct S p1, u32 p2)
{
    i8 l0 = ((i8)0x80U);
    i16 l1 = ((i16)ga0[(u32)p2 & 7]);
    u32 la[8] = { ((u32)0x0U), ((u32)0xffffffffU), ((u32)0x7fffffffU), ((u32)0x2U), ((u32)0x6f4e47d1U), ((u32)0x0U), ((u32)0xffffffffU), ((u32)0x57a5c9acU) };
    return (i16)((u64)l0 ^ (u64)l1 ^ (u64)((i16)0x8000U) ^ (u64)la[(u32)((u32)0x1U) & 7]);
}
int main(void)
{
    mix_s(f1(((i16)0x0U), ((i64)((u64)((i32)g_b.u13) | (u64)((i16)0x4112U))), ((u64)fptab[((u32)g3) & 3](((u32)0x0U))), ((i32)fptab[((u32)0xffffffffU) & 3](((u32)g1)))));
    mix((u64)f2(((u32)0x80000000U), ((i32)gm[((u32)0xf6392b54U) & 3][((u32)g3) % 5])));
    show("end");
}
