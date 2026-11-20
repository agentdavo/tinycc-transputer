/* Regression: a range rule took a u16 cast's mask for redundant.  The value
 * range simulation walked back over a jump target on PFIX 0 padding into
 * the other arm of a ?: , saw only that arm's constant, and dropped the
 * `ldc 65535; and`.  Reduced from fuzz seed 42149. */
int printf(const char *, ...);
typedef unsigned char u8; typedef short i16; typedef unsigned short u16;
typedef int i32; typedef unsigned u32; typedef long long i64; typedef unsigned long long u64;
struct B { unsigned u1 : 1; unsigned u3 : 3; unsigned u7 : 7; unsigned u13 : 13; int s5 : 5; unsigned u8x : 8; };
static struct B g_b;
static i16 g4 = ((i16)0x8000U);
static i64 g3 = 0;
static u64 f4(void)
{
    i16 l1 = g4;
    u16 l2 = ((u16)((u32)((i32)g_b.u7) - (u32)((i64)(((u16)g3) ? ((i64)0x7fffffffffffffffULL) : ((i64)0x7663e307eb428276ULL)))));
    printf("l1 %d l2 %u\n", l1, l2);
    return (u64)l1 ^ (u64)l2;
}
int main(void) { u64 r = f4(); printf("r %08x%08x\n", (u32)(r >> 32), (u32)r); return 0; }
