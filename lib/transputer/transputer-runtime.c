/* Transputer 64-bit arithmetic runtime for libtcc1.
 *
 * Division uses 32-bit operations internally. Logical 64-bit shifts and
 * negation rely on native lowering; code-generation tests guard the shifts
 * against recursive calls to these helpers.
 */

typedef long long s64;
typedef unsigned long long u64;
typedef unsigned int u32;

u32 __builtin_bswap32(u32 value) {
    return ((value & 0x000000ffu) << 24)
         | ((value & 0x0000ff00u) << 8)
         | ((value & 0x00ff0000u) >> 8)
         | ((value & 0xff000000u) >> 24);
}

typedef union {
    u64 v;
    struct { u32 lo; u32 hi; } p;
} u64_u;

static u64 u64_neg(u64 value) {
    return 0ULL - value;
}

static u64 u64_udivmod(u64_u dividend, u64_u divisor, u64_u *rem_out) {
    u64_u result, remainder;
    int i;
    result.p.hi = 0; result.p.lo = 0;
    remainder.p.hi = 0; remainder.p.lo = 0;
    if (divisor.p.hi == 0 && dividend.p.hi == 0) {
        result.p.lo = dividend.p.lo / divisor.p.lo;
        remainder.p.lo = dividend.p.lo % divisor.p.lo;
        if (rem_out) *rem_out = remainder;
        return result.v;
    }
    if (divisor.p.hi == 0 && divisor.p.lo <= 0xffffu) {
        u32 part;
        result.p.hi = dividend.p.hi / divisor.p.lo;
        remainder.p.lo = dividend.p.hi % divisor.p.lo;
        part = (remainder.p.lo << 16) | (dividend.p.lo >> 16);
        result.p.lo = (part / divisor.p.lo) << 16;
        remainder.p.lo = part % divisor.p.lo;
        part = (remainder.p.lo << 16) | (dividend.p.lo & 0xffffu);
        result.p.lo |= part / divisor.p.lo;
        remainder.p.lo = part % divisor.p.lo;
        if (rem_out) *rem_out = remainder;
        return result.v;
    }
    if (divisor.p.hi == 0 && (divisor.p.lo & (divisor.p.lo - 1)) == 0) {
        u32 mask = divisor.p.lo - 1;
        u32 power = divisor.p.lo;
#ifdef __TRANSPUTER__
        int shift = 31 - __norm(power);
#else
        int shift = 0;

        while (power > 1) {
            power >>= 1;
            shift++;
        }
#endif
        result.p.hi = dividend.p.hi >> shift;
        result.p.lo = dividend.p.lo >> shift;
        if (shift)
            result.p.lo |= dividend.p.hi << (32 - shift);
        remainder.p.lo = dividend.p.lo & mask;
        if (rem_out) *rem_out = remainder;
        return result.v;
    }
    if (divisor.p.hi != 0 && divisor.p.lo == 0 &&
        (divisor.p.hi & (divisor.p.hi - 1)) == 0) {
        u32 power = divisor.p.hi;
#ifdef __TRANSPUTER__
        int shift = power == 1 ? 0 : 31 - __norm(power);
#else
        int shift = 0;

        while (power > 1) {
            power >>= 1;
            shift++;
        }
#endif
        result.p.lo = dividend.p.hi >> shift;
        remainder.p.hi = dividend.p.hi & (divisor.p.hi - 1);
        remainder.p.lo = dividend.p.lo;
        if (rem_out) *rem_out = remainder;
        return result.v;
    }
    if (divisor.p.hi == 0) {
        /* The high quotient word can be computed directly. The remaining
         * loop divides the low word with that operation's remainder. */
        result.p.hi = dividend.p.hi / divisor.p.lo;
        remainder.p.lo = dividend.p.hi % divisor.p.lo;
    } else {
        /* A divisor of at least 2^32 is greater than the initial high word,
         * so the first 32 restoring steps produce no quotient bits. */
        remainder.p.lo = dividend.p.hi;
    }
    for (i = 31; i >= 0; i--) {
        remainder.p.hi = (remainder.p.hi << 1) | (remainder.p.lo >> 31);
        remainder.p.lo = (remainder.p.lo << 1) | ((dividend.p.lo >> i) & 1u);
        if (remainder.p.hi > divisor.p.hi ||
            (remainder.p.hi == divisor.p.hi && remainder.p.lo >= divisor.p.lo)) {
            u32 borrow = remainder.p.lo < divisor.p.lo;
            remainder.p.lo -= divisor.p.lo;
            remainder.p.hi -= divisor.p.hi + borrow;
            result.p.lo |= (1u << i);
        }
    }
    if (rem_out) *rem_out = remainder;
    return result.v;
}

s64 __divdi3(s64 dividend, s64 divisor) {
    int neg = 0;
    u64_u a, b;
    u64_u q;
    a.v = (u64)dividend;
    b.v = (u64)divisor;
    if (b.p.hi == 0 && b.p.lo == 0) return 0;
    if (a.p.hi & 0x80000000u) { a.v = u64_neg(a.v); neg ^= 1; }
    if (b.p.hi & 0x80000000u) { b.v = u64_neg(b.v); neg ^= 1; }
    q.v = u64_udivmod(a, b, 0);
    if (neg) q.v = u64_neg(q.v);
    return (s64)q.v;
}

u64 __udivdi3(u64 dividend, u64 divisor) {
    u64_u a, b;
    a.v = dividend;
    b.v = divisor;
    if (b.p.hi == 0 && b.p.lo == 0) return 0;
    return u64_udivmod(a, b, 0);
}

s64 __moddi3(s64 dividend, s64 divisor) {
    int neg = 0;
    u64_u a, b;
    u64_u r;
    a.v = (u64)dividend;
    b.v = (u64)divisor;
    if (b.p.hi == 0 && b.p.lo == 0) return 0;
    if (a.p.hi & 0x80000000u) { a.v = u64_neg(a.v); neg ^= 1; }
    if (b.p.hi & 0x80000000u) b.v = u64_neg(b.v);
    u64_udivmod(a, b, &r);
    if (neg) r.v = u64_neg(r.v);
    return (s64)r.v;
}

u64 __umoddi3(u64 dividend, u64 divisor) {
    u64_u a, b;
    u64_u r;
    a.v = dividend;
    b.v = divisor;
    if (b.p.hi == 0 && b.p.lo == 0) return 0;
    u64_udivmod(a, b, &r);
    return r.v;
}

s64 __ashldi3(s64 value, int shift) {
    if (shift >= 64) return 0;
    return (s64)((u64)value << shift);
}

s64 __ashrdi3(s64 value, int shift) {
    u64_u v, r;
    u32 sign;
    v.v = (u64)value;
    sign = (v.p.hi & 0x80000000u) ? 0xFFFFFFFFu : 0u;
    if (shift >= 64) return sign ? -1 : 0;
    if (shift == 0) return value;
    if (shift >= 32) {
        /* v.p.hi is unsigned, so its >> brings in zeros: the sign has to be
         * put into the vacated top bits (-5 >> 33 gave 0xffffffff7fffffff) */
        r.p.lo = shift == 32 ? v.p.hi
                             : (v.p.hi >> (shift - 32)) | (sign << (64 - shift));
        r.p.hi = sign;
    } else {
        r.p.lo = (v.p.lo >> shift) | (v.p.hi << (32 - shift));
        r.p.hi = (v.p.hi >> shift) | (sign << (32 - shift));
    }
    return (s64)r.v;
}

u64 __lshrdi3(u64 value, int shift) {
    if (shift >= 64) return 0;
    return value >> shift;
}
