/* Soft-float runtime for -mt425: the IMS T425 (like the T414) has no FPU.
 *
 * transputer-gen.c turns every floating-point operation into a call to one
 * of these libgcc-named helpers: IEEE 754 binary32 and binary64 with
 * round-to-nearest-even, subnormals, signed zeros, infinities and NaNs; no
 * exception flags and no other rounding modes.  A float travels as one
 * word and a double as two, exactly like an int and a long long, so the
 * helpers are written over integer types and use no floating point
 * themselves.  The long long conversions __fix[uns]{s,d}fdi and
 * __floatundi{s,d}f come from libtcc1.c, the 16-bit float conversions from
 * transputer-f16.c.
 *
 * Built only into transputer-libtcc1-t425.a and -t414.a.  Plain C, so it
 * also builds on the host to be checked against a hardware FPU.
 *
 * On the transputer, the single-length arithmetic and relations, float to
 * and from int, unsigned and double, and double to and from int and
 * unsigned are transputer-softfp-sn.S, on the T414/T425 floating-point
 * support instructions, and double add, subtract, multiply, divide and the
 * relations are transputer-softfp-db.S: the C ones here are renamed SN(),
 * __tp_addsf3 and so on, and those files hand them their NaN, infinity,
 * zero and out-of-range cases.  They are also what those are checked
 * against.
 */

#ifdef __TRANSPUTER__
# define SN(f) __tp_##f
#else
# define SN(f) __##f
#endif

typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;
typedef long long s64;

#define F_SIGN  0x80000000u
#define F_INF   0x7f800000u
#define F_QNAN  0x7fc00000u
#define D_SIGN  0x8000000000000000ULL
#define D_INF   0x7ff0000000000000ULL
#define D_QNAN  0x7ff8000000000000ULL
#define D_FRAC  0x000fffffffffffffULL

#define F_ISNAN(a) (((a) & ~F_SIGN) > F_INF)
#define D_ISNAN(a) (((a) & ~D_SIGN) > D_INF)

/* m >> n with every bit shifted out ORed into bit 0 */
static u64 shr_jam(u64 m, int n)
{
    if (n <= 0)
        return m;
    if (n >= 64)
        return m != 0;
    return (m >> n) | ((m << (64 - n)) != 0);
}

/* leading zeros of m != 0 */
static int clz64(u64 m)
{
    int n = 0;

    if (!(m >> 32)) { n += 32; m <<= 32; }
    if (!(m >> 48)) { n += 16; m <<= 16; }
    if (!(m >> 56)) { n += 8;  m <<= 8;  }
    if (!(m >> 60)) { n += 4;  m <<= 4;  }
    if (!(m >> 62)) { n += 2;  m <<= 2;  }
    if (!(m >> 63)) n += 1;
    return n;
}

/* The float nearest sign * m * 2^e, ties to even; m != 0, and any bits
   already lost are ORed into bit 0 of m.  After normalising, m's top bit
   has weight 2^(E-127): the significand is m >> 40, the rest rounds.
   Adding (E-1) << 23 lets the hidden bit, or a carry out of rounding,
   step the exponent, up to infinity. */
static u32 f_round(u32 sign, int e, u64 m)
{
    int n = clz64(m), E;
    u32 sig;
    u64 rem;

    m <<= n;
    E = e - n + 190;
    if (E >= 255)
        return sign | F_INF;
    if (E <= 0) {                       /* subnormal: no hidden bit */
        m = shr_jam(m, 1 - E);
        E = 1;
    }
    sig = (u32)(m >> 40);
    rem = m & 0xffffffffffULL;
    if (rem > 0x8000000000ULL || (rem == 0x8000000000ULL && (sig & 1)))
        sig++;
    return sign | (((u32)(E - 1) << 23) + sig);
}

/* The same for a double: the significand is m >> 11 */
static u64 d_round(u64 sign, int e, u64 m)
{
    int n = clz64(m), E;
    u64 sig, rem;

    m <<= n;
    E = e - n + 1086;
    if (E >= 2047)
        return sign | D_INF;
    if (E <= 0) {
        m = shr_jam(m, 1 - E);
        E = 1;
    }
    sig = m >> 11;
    rem = m & 0x7ff;
    if (rem > 0x400 || (rem == 0x400 && (sig & 1)))
        sig++;
    return sign | (((u64)(E - 1) << 52) + sig);
}

/* A finite a is sig * 2^e; sig is 0 for a zero */
static int f_unpack(u32 a, u32 *sig)
{
    int E = (a >> 23) & 0xff;

    *sig = a & 0x7fffff;
    if (!E)
        return -149;
    *sig |= 0x800000;
    return E - 150;
}

static int d_unpack(u64 a, u64 *sig)
{
    int E = (int)(a >> 52) & 0x7ff;

    *sig = a & D_FRAC;
    if (!E)
        return -1074;
    *sig |= D_FRAC + 1;
    return E - 1075;
}

/* Move a subnormal's leading 1 up to the hidden bit's place */
static int f_norm(u32 *sig, int e)
{
    while (!(*sig & 0x800000)) {
        *sig <<= 1;
        e--;
    }
    return e;
}

static int d_norm(u64 *sig, int e)
{
    while (!(*sig >> 52)) {
        *sig <<= 1;
        e--;
    }
    return e;
}

/* Quiet a NaN operand of a binary operation */
static u32 f_nan(u32 a, u32 b)
{
    return (F_ISNAN(a) ? a : b) | 0x400000;
}

static u64 d_nan(u64 a, u64 b)
{
    return (D_ISNAN(a) ? a : b) | 0x0008000000000000ULL;
}

/* ---------------------------------------------------------------- */

static u32 f_add(u32 a, u32 b)
{
    u32 sa, sb, sign;
    int ea, eb;
    u64 ma, mb, m;

    if (F_ISNAN(a) || F_ISNAN(b))
        return f_nan(a, b);
    if ((a & ~F_SIGN) == F_INF) {
        if ((b & ~F_SIGN) == F_INF && (a ^ b) & F_SIGN)
            return F_QNAN;              /* inf - inf */
        return a;
    }
    if ((b & ~F_SIGN) == F_INF)
        return b;
    if (!(a & ~F_SIGN))                 /* +0 + -0 = +0, -0 + -0 = -0 */
        return (b & ~F_SIGN) ? b : a & b;
    if (!(b & ~F_SIGN))
        return a;

    ea = f_unpack(a, &sa);
    eb = f_unpack(b, &sb);
    if (ea < eb || (ea == eb && sa < sb)) {
        u32 t = a; a = b; b = t;
        t = sa; sa = sb; sb = t;
        ea ^= eb; eb ^= ea; ea ^= eb;
    }
    /* |a| >= |b|; 39 guard bits below the significands */
    sign = a & F_SIGN;
    ma = (u64)sa << 39;
    mb = shr_jam((u64)sb << 39, ea - eb);
    if ((a ^ b) & F_SIGN) {
        m = ma - mb;
        if (!m)
            return 0;                   /* x - x = +0 */
    } else {
        m = ma + mb;
    }
    return f_round(sign, ea - 39, m);
}

u32 SN(addsf3)(u32 a, u32 b) { return f_add(a, b); }
u32 SN(subsf3)(u32 a, u32 b) { return f_add(a, b ^ F_SIGN); }
u32 __negsf2(u32 a) { return a ^ F_SIGN; }

u32 SN(mulsf3)(u32 a, u32 b)
{
    u32 sign = (a ^ b) & F_SIGN, sa, sb;
    int ea, eb;

    if (F_ISNAN(a) || F_ISNAN(b))
        return f_nan(a, b);
    if ((a & ~F_SIGN) == F_INF || (b & ~F_SIGN) == F_INF) {
        if (!(a & ~F_SIGN) || !(b & ~F_SIGN))
            return F_QNAN;              /* inf * 0 */
        return sign | F_INF;
    }
    if (!(a & ~F_SIGN) || !(b & ~F_SIGN))
        return sign;
    ea = f_unpack(a, &sa);
    eb = f_unpack(b, &sb);
    return f_round(sign, ea + eb, (u64)sa * sb);
}

u32 SN(divsf3)(u32 a, u32 b)
{
    u32 sign = (a ^ b) & F_SIGN, sa, sb, q, r;
    int ea, eb, i;

    if (F_ISNAN(a) || F_ISNAN(b))
        return f_nan(a, b);
    if ((a & ~F_SIGN) == F_INF)
        return (b & ~F_SIGN) == F_INF ? F_QNAN : sign | F_INF;
    if ((b & ~F_SIGN) == F_INF)
        return sign;
    if (!(b & ~F_SIGN))
        return (a & ~F_SIGN) ? sign | F_INF : F_QNAN;
    if (!(a & ~F_SIGN))
        return sign;
    ea = f_norm(&sa, f_unpack(a, &sa));
    eb = f_norm(&sb, f_unpack(b, &sb));
    if (sa < sb) {
        sa <<= 1;
        ea--;
    }
    /* sb <= sa < 2 sb: 26 quotient bits, the first a 1 */
    q = 0;
    r = sa;
    for (i = 0; i < 26; i++) {
        q <<= 1;
        if (r >= sb) {
            r -= sb;
            q |= 1;
        }
        r <<= 1;
    }
    return f_round(sign, ea - eb - 26, ((u64)q << 1) | (r != 0));
}

/* ---------------------------------------------------------------- */

static u64 d_add(u64 a, u64 b)
{
    u64 sa, sb, sign, ma, mb, m;
    int ea, eb;

    if (D_ISNAN(a) || D_ISNAN(b))
        return d_nan(a, b);
    if ((a & ~D_SIGN) == D_INF) {
        if ((b & ~D_SIGN) == D_INF && (a ^ b) & D_SIGN)
            return D_QNAN;
        return a;
    }
    if ((b & ~D_SIGN) == D_INF)
        return b;
    if (!(a & ~D_SIGN))
        return (b & ~D_SIGN) ? b : a & b;
    if (!(b & ~D_SIGN))
        return a;

    ea = d_unpack(a, &sa);
    eb = d_unpack(b, &sb);
    if (ea < eb || (ea == eb && sa < sb)) {
        u64 t = a; a = b; b = t;
        t = sa; sa = sb; sb = t;
        ea ^= eb; eb ^= ea; ea ^= eb;
    }
    /* |a| >= |b|; 10 guard bits below the significands */
    sign = a & D_SIGN;
    ma = sa << 10;
    mb = shr_jam(sb << 10, ea - eb);
    if ((a ^ b) & D_SIGN) {
        m = ma - mb;
        if (!m)
            return 0;
    } else {
        m = ma + mb;
    }
    return d_round(sign, ea - 10, m);
}

u64 SN(adddf3)(u64 a, u64 b) { return d_add(a, b); }
u64 SN(subdf3)(u64 a, u64 b) { return d_add(a, b ^ D_SIGN); }
u64 __negdf2(u64 a) { return a ^ D_SIGN; }

u64 SN(muldf3)(u64 a, u64 b)
{
    u64 sign = (a ^ b) & D_SIGN, sa, sb;
    u64 al, ah, bl, bh, ll, lh, hl, mid, hi, lo;
    int ea, eb;

    if (D_ISNAN(a) || D_ISNAN(b))
        return d_nan(a, b);
    if ((a & ~D_SIGN) == D_INF || (b & ~D_SIGN) == D_INF) {
        if (!(a & ~D_SIGN) || !(b & ~D_SIGN))
            return D_QNAN;
        return sign | D_INF;
    }
    if (!(a & ~D_SIGN) || !(b & ~D_SIGN))
        return sign;
    ea = d_norm(&sa, d_unpack(a, &sa));
    eb = d_norm(&sb, d_unpack(b, &sb));
    /* both significands up to bit 63, then the top half of their
       128-bit product, from 32 x 32 bit partial products */
    sa <<= 11;
    sb <<= 11;
    al = (u32)sa; ah = sa >> 32;
    bl = (u32)sb; bh = sb >> 32;
    ll = al * bl;
    lh = al * bh;
    hl = ah * bl;
    mid = (ll >> 32) + (u32)lh + (u32)hl;
    lo = (mid << 32) | (u32)ll;
    hi = ah * bh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    return d_round(sign, ea + eb + 42, hi | (lo != 0));
}

u64 SN(divdf3)(u64 a, u64 b)
{
    u64 sign = (a ^ b) & D_SIGN, sa, sb, q, r;
    int ea, eb, i;

    if (D_ISNAN(a) || D_ISNAN(b))
        return d_nan(a, b);
    if ((a & ~D_SIGN) == D_INF)
        return (b & ~D_SIGN) == D_INF ? D_QNAN : sign | D_INF;
    if ((b & ~D_SIGN) == D_INF)
        return sign;
    if (!(b & ~D_SIGN))
        return (a & ~D_SIGN) ? sign | D_INF : D_QNAN;
    if (!(a & ~D_SIGN))
        return sign;
    ea = d_norm(&sa, d_unpack(a, &sa));
    eb = d_norm(&sb, d_unpack(b, &sb));
    if (sa < sb) {
        sa <<= 1;
        ea--;
    }
    /* sb <= sa < 2 sb: 55 quotient bits, the first a 1 */
    q = 0;
    r = sa;
    for (i = 0; i < 55; i++) {
        q <<= 1;
        if (r >= sb) {
            r -= sb;
            q |= 1;
        }
        r <<= 1;
    }
    return d_round(sign, ea - eb - 55, (q << 1) | (r != 0));
}

/* ---------------------------------------------------------------- */

/* -1, 0 or 1 as a <, == or > b; `unordered` when either is a NaN */
static int f_cmp(u32 a, u32 b, int unordered)
{
    if (F_ISNAN(a) || F_ISNAN(b))
        return unordered;
    if (!((a | b) & ~F_SIGN))           /* +0 == -0 */
        return 0;
    if ((a ^ b) & F_SIGN)
        return (a & F_SIGN) ? -1 : 1;
    if (a == b)
        return 0;
    return ((a < b) ^ !!(a & F_SIGN)) ? -1 : 1;
}

static int d_cmp(u64 a, u64 b, int unordered)
{
    if (D_ISNAN(a) || D_ISNAN(b))
        return unordered;
    if (!((a | b) & ~D_SIGN))
        return 0;
    if ((a ^ b) & D_SIGN)
        return (a & D_SIGN) ? -1 : 1;
    if (a == b)
        return 0;
    return ((a < b) ^ !!(a & D_SIGN)) ? -1 : 1;
}

/* a OP b holds when __OPsf2(a, b) OP 0, so an unordered pair answers 1
   for ==, !=, <, <= and -1 for >, >= */
int SN(eqsf2)(u32 a, u32 b) { return f_cmp(a, b, 1); }
int SN(nesf2)(u32 a, u32 b) { return f_cmp(a, b, 1); }
int SN(ltsf2)(u32 a, u32 b) { return f_cmp(a, b, 1); }
int SN(lesf2)(u32 a, u32 b) { return f_cmp(a, b, 1); }
int SN(gtsf2)(u32 a, u32 b) { return f_cmp(a, b, -1); }
int SN(gesf2)(u32 a, u32 b) { return f_cmp(a, b, -1); }
int SN(unordsf2)(u32 a, u32 b) { return F_ISNAN(a) || F_ISNAN(b); }

int SN(eqdf2)(u64 a, u64 b) { return d_cmp(a, b, 1); }
int SN(nedf2)(u64 a, u64 b) { return d_cmp(a, b, 1); }
int SN(ltdf2)(u64 a, u64 b) { return d_cmp(a, b, 1); }
int SN(ledf2)(u64 a, u64 b) { return d_cmp(a, b, 1); }
int SN(gtdf2)(u64 a, u64 b) { return d_cmp(a, b, -1); }
int SN(gedf2)(u64 a, u64 b) { return d_cmp(a, b, -1); }
int SN(unorddf2)(u64 a, u64 b) { return D_ISNAN(a) || D_ISNAN(b); }

/* ---------------------------------------------------------------- */

u32 SN(floatsisf)(s32 i)
{
    if (!i)
        return 0;
    if (i < 0)
        return f_round(F_SIGN, 0, -(u64)i);
    return f_round(0, 0, (u64)i);
}

u32 SN(floatunsisf)(u32 u)
{
    return u ? f_round(0, 0, u) : 0;
}

u32 __floatdisf(s64 i)
{
    if (!i)
        return 0;
    if (i < 0)
        return f_round(F_SIGN, 0, -(u64)i);
    return f_round(0, 0, (u64)i);
}

u64 SN(floatsidf)(s32 i)
{
    if (!i)
        return 0;
    if (i < 0)
        return d_round(D_SIGN, 0, -(u64)i);
    return d_round(0, 0, (u64)i);
}

u64 SN(floatunsidf)(u32 u)
{
    return u ? d_round(0, 0, u) : 0;
}

u64 __floatdidf(s64 i)
{
    if (!i)
        return 0;
    if (i < 0)
        return d_round(D_SIGN, 0, -(u64)i);
    return d_round(0, 0, (u64)i);
}

/* Conversions to integer truncate; a NaN gives 0 and anything out of
   range saturates (both undefined behaviour in C). */
s32 SN(fixsfsi)(u32 a)
{
    int E = (a >> 23) & 0xff;
    u32 sig = (a & 0x7fffff) | 0x800000, v;

    if (F_ISNAN(a) || E < 127)
        return 0;
    if (E >= 127 + 31)
        return (a & F_SIGN) ? -0x7fffffff - 1 : 0x7fffffff;
    v = E >= 150 ? sig << (E - 150) : sig >> (150 - E);
    return (a & F_SIGN) ? -(s32)v : (s32)v;
}

u32 SN(fixunssfsi)(u32 a)
{
    int E = (a >> 23) & 0xff;
    u32 sig = (a & 0x7fffff) | 0x800000;

    if (F_ISNAN(a) || E < 127 || (a & F_SIGN))
        return 0;
    if (E >= 127 + 32)
        return 0xffffffff;
    return E >= 150 ? sig << (E - 150) : sig >> (150 - E);
}

s32 SN(fixdfsi)(u64 a)
{
    int E = (int)(a >> 52) & 0x7ff;
    u32 v;

    if (D_ISNAN(a) || E < 1023)
        return 0;
    if (E >= 1023 + 31)
        return (a & D_SIGN) ? -0x7fffffff - 1 : 0x7fffffff;
    v = (u32)(((a & D_FRAC) | (D_FRAC + 1)) >> (1075 - E));
    return (a & D_SIGN) ? -(s32)v : (s32)v;
}

u32 SN(fixunsdfsi)(u64 a)
{
    int E = (int)(a >> 52) & 0x7ff;

    if (D_ISNAN(a) || E < 1023 || (a & D_SIGN))
        return 0;
    if (E >= 1023 + 32)
        return 0xffffffff;
    return (u32)(((a & D_FRAC) | (D_FRAC + 1)) >> (1075 - E));
}

/* ---------------------------------------------------------------- */

u64 SN(extendsfdf2)(u32 a)
{
    u64 sign = (u64)(a & F_SIGN) << 32;
    u32 sig;
    int E = (a >> 23) & 0xff, e;

    if (E == 0xff)                      /* inf, or a NaN made quiet */
        return sign | D_INF | ((u64)(a & 0x7fffff) << 29)
               | ((a & 0x7fffff) ? 0x0008000000000000ULL : 0);
    if (!(a & ~F_SIGN))
        return sign;
    e = f_unpack(a, &sig);
    return d_round(sign, e, sig);                       /* exact */
}

u32 SN(truncdfsf2)(u64 a)
{
    u32 sign = (u32)(a >> 32) & F_SIGN;
    u64 sig;
    int E = (int)(a >> 52) & 0x7ff, e;

    if (E == 0x7ff)
        return sign | F_INF
               | ((a & D_FRAC) ? 0x400000 | (u32)((a & D_FRAC) >> 29) : 0);
    if (!(a & ~D_SIGN))
        return sign;
    e = d_unpack(a, &sig);
    return f_round(sign, e, sig);
}

/* ---------------------------------------------------------------- */

/* floor(sqrt(sig * 4^z)) for sig < 4^n, one result bit per pair of
   radicand bits; *inexact says whether a remainder was left */
static u64 isqrt(u64 sig, int n, int z, int *inexact)
{
    u64 q = 0, r = 0;
    int i;

    for (i = n - 1; i >= -z; i--) {
        r = (r << 2) | (i >= 0 ? (sig >> 2 * i) & 3 : 0);
        if (r >= (q << 2 | 1)) {
            r -= q << 2 | 1;
            q = q << 1 | 1;
        } else {
            q <<= 1;
        }
    }
    *inexact = r != 0;
    return q;
}

/* Correctly rounded square roots, for __fpsqrt under -mt425.  An even
   exponent halves exactly; 27 or 56 result bits and the remainder leave
   enough below the significand to round. */
u32 __sqrtsf2(u32 a)
{
    u32 sig;
    int e, inexact;
    u64 q;

    if (F_ISNAN(a))
        return a | 0x400000;
    if (!(a & ~F_SIGN) || a == F_INF)   /* +-0, +inf */
        return a;
    if (a & F_SIGN)
        return F_QNAN;
    e = f_norm(&sig, f_unpack(a, &sig));
    if (e & 1) {
        sig <<= 1;
        e--;
    }
    q = isqrt(sig, 13, 15, &inexact);   /* sqrt(sig * 2^30) */
    return f_round(0, (e - 30) / 2 - 1, q << 1 | inexact);
}

u64 __sqrtdf2(u64 a)
{
    u64 sig, q;
    int e, inexact;

    if (D_ISNAN(a))
        return a | 0x0008000000000000ULL;
    if (!(a & ~D_SIGN) || a == D_INF)
        return a;
    if (a & D_SIGN)
        return D_QNAN;
    e = d_norm(&sig, d_unpack(a, &sig));
    if (e & 1) {
        sig <<= 1;
        e--;
    }
    q = isqrt(sig, 27, 29, &inexact);   /* sqrt(sig * 2^58) */
    return d_round(0, (e - 58) / 2 - 1, q << 1 | inexact);
}
