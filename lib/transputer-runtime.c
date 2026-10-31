/* Transputer 64-bit arithmetic runtime for libtcc1.
 *
 * These routines intentionally use only 32-bit operations internally, so
 * compiling the runtime cannot recursively introduce calls to itself.
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

static u64_u u64_from(u64 v) { u64_u u; u.v = v; return u; }

static int u64_cmp(u64_u a, u64_u b) {
    if (a.p.hi != b.p.hi) return a.p.hi > b.p.hi ? 1 : -1;
    if (a.p.lo != b.p.lo) return a.p.lo > b.p.lo ? 1 : -1;
    return 0;
}

static u64_u u64_sub(u64_u a, u64_u b) {
    u64_u r;
    r.p.lo = a.p.lo - b.p.lo;
    r.p.hi = a.p.hi - b.p.hi - (a.p.lo < b.p.lo);
    return r;
}

static u64_u u64_add(u64_u a, u64_u b) {
    u64_u r;
    r.p.lo = a.p.lo + b.p.lo;
    r.p.hi = a.p.hi + b.p.hi + (r.p.lo < a.p.lo);
    return r;
}

static u64_u u64_neg(u64_u v) {
    u64_u inv, one;
    inv.p.lo = ~v.p.lo; inv.p.hi = ~v.p.hi;
    one.p.lo = 1; one.p.hi = 0;
    return u64_add(inv, one);
}

static u64_u u64_shl1(u64_u v) {
    u64_u r;
    r.p.hi = (v.p.hi << 1) | (v.p.lo >> 31);
    r.p.lo = v.p.lo << 1;
    return r;
}

static u64_u u64_udivmod(u64_u dividend, u64_u divisor, u64_u *rem_out) {
    u64_u result, remainder;
    int i;
    result.p.hi = 0; result.p.lo = 0;
    remainder.p.hi = 0; remainder.p.lo = 0;
    for (i = 63; i >= 0; i--) {
        remainder = u64_shl1(remainder);
        if (i >= 32)
            remainder.p.lo |= (dividend.p.hi >> (i - 32)) & 1u;
        else
            remainder.p.lo |= (dividend.p.lo >> i) & 1u;
        if (u64_cmp(remainder, divisor) >= 0) {
            remainder = u64_sub(remainder, divisor);
            if (i >= 32)
                result.p.hi |= (1u << (i - 32));
            else
                result.p.lo |= (1u << i);
        }
    }
    if (rem_out) *rem_out = remainder;
    return result;
}

s64 __divdi3(s64 dividend, s64 divisor) {
    int neg = 0;
    u64_u a = u64_from((u64)dividend);
    u64_u b = u64_from((u64)divisor);
    u64_u q;
    if (b.p.hi == 0 && b.p.lo == 0) return 0;
    if (a.p.hi & 0x80000000u) { a = u64_neg(a); neg ^= 1; }
    if (b.p.hi & 0x80000000u) { b = u64_neg(b); neg ^= 1; }
    q = u64_udivmod(a, b, 0);
    if (neg) q = u64_neg(q);
    return (s64)q.v;
}

u64 __udivdi3(u64 dividend, u64 divisor) {
    u64_u a = u64_from(dividend);
    u64_u b = u64_from(divisor);
    if (b.p.hi == 0 && b.p.lo == 0) return 0;
    return u64_udivmod(a, b, 0).v;
}

s64 __moddi3(s64 dividend, s64 divisor) {
    int neg = 0;
    u64_u a = u64_from((u64)dividend);
    u64_u b = u64_from((u64)divisor);
    u64_u r;
    if (b.p.hi == 0 && b.p.lo == 0) return 0;
    if (a.p.hi & 0x80000000u) { a = u64_neg(a); neg ^= 1; }
    if (b.p.hi & 0x80000000u) b = u64_neg(b);
    u64_udivmod(a, b, &r);
    if (neg) r = u64_neg(r);
    return (s64)r.v;
}

u64 __umoddi3(u64 dividend, u64 divisor) {
    u64_u a = u64_from(dividend);
    u64_u b = u64_from(divisor);
    u64_u r;
    if (b.p.hi == 0 && b.p.lo == 0) return 0;
    u64_udivmod(a, b, &r);
    return r.v;
}

s64 __ashldi3(s64 value, int shift) {
    u64_u v = u64_from((u64)value);
    u64_u r;
    if (shift >= 64) return 0;
    if (shift == 0) return value;
    if (shift >= 32) {
        r.p.hi = v.p.lo << (shift - 32);
        r.p.lo = 0;
    } else {
        r.p.hi = (v.p.hi << shift) | (v.p.lo >> (32 - shift));
        r.p.lo = v.p.lo << shift;
    }
    return (s64)r.v;
}

s64 __ashrdi3(s64 value, int shift) {
    u64_u v = u64_from((u64)value);
    u64_u r;
    u32 sign = (v.p.hi & 0x80000000u) ? 0xFFFFFFFFu : 0u;
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
    u64_u v = u64_from(value);
    u64_u r;
    if (shift >= 64) return 0;
    if (shift == 0) return value;
    if (shift >= 32) {
        r.p.lo = v.p.hi >> (shift - 32);
        r.p.hi = 0;
    } else {
        r.p.lo = (v.p.lo >> shift) | (v.p.hi << (32 - shift));
        r.p.hi = v.p.hi >> shift;
    }
    return r.v;
}
