/* The 16-bit float conversions, for every target without TVX: binary16
 * (_Float16, __fp16) and bfloat16, held in the low half of a word, bit for
 * bit as the TVX conversions in tvx/tvx.h.  transputer-gen.c calls
 * these where -mt9000tvx would emit F16TOR32, R32TOF16 and R32TOBF16;
 * bfloat16 -> float is a shift, done inline.  A float travels as one word,
 * so they are written over integers and use no floating point.
 *
 * Built into every transputer runtime (transputer-libtcc1*.a).  Plain C, so
 * it also builds on the host to be checked against tvx/tvx.h.
 */

typedef unsigned int u32;

#define F_SIGN  0x80000000u
#define F_INF   0x7f800000u

#define F_ISNAN(a) (((a) & ~F_SIGN) > F_INF)

u32 __extendhfsf2(u32 x)
{
    u32 sign = (x & 0x8000) << 16, e = (x >> 10) & 0x1f, m = x & 0x3ff;

    if (e == 0) {
        if (m == 0)
            return sign;
        e = 1;                          /* subnormal: renormalise */
        while (!(m & 0x400)) {
            m <<= 1;
            e--;
        }
        m &= 0x3ff;
    } else if (e == 31) {
        return sign | F_INF | m << 13;  /* inf, NaN */
    }
    return sign | (e + 112) << 23 | m << 13;
}

u32 __truncsfhf2(u32 x)
{
    u32 sign = (x >> 16) & 0x8000, m = x & 0x7fffff, r;
    int e = (int)((x >> 23) & 0xff) - 127 + 15;

    if (((x >> 23) & 0xff) == 0xff)     /* inf, or a NaN made quiet */
        return m ? sign | 0x7e00 | m >> 13 : sign | 0x7c00;
    if (e >= 31)
        return sign | 0x7c00;
    if (e <= 0) {                       /* subnormal or zero */
        if (e < -10)
            return sign;
        m |= 0x800000;
        r = m >> (14 - e);
        if ((m >> (13 - e)) & 1 && ((m & ((1u << (13 - e)) - 1)) || (r & 1)))
            r++;
        return sign | r;
    }
    r = (u32)e << 10 | m >> 13;
    if ((m >> 12) & 1 && ((m & 0xfff) || (r & 1)))
        r++;                            /* may carry into the exponent */
    return sign | r;
}

u32 __truncsfbf2(u32 x)
{
    if (F_ISNAN(x))
        return ((x >> 16) & 0xffff) | 0x40;
    return ((x + 0x7fff + ((x >> 16) & 1)) >> 16) & 0xffff;
}
