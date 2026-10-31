/*
 * transputer-softfp-sn.S - single-length soft float on the T414/T425
 * floating-point support instructions.
 *
 * The IMS T414 and T425 have no FPU, but microcode for the steps every
 * single-length operation repeats (TCWG 7.11, F.3.6):
 *
 *   unpacksn    A = x: A' = fraction, its leading 1 at bit 31 when x is
 *               normalised, B' = biased exponent (1 for a denormal),
 *               C' = 4 B + type (0 zero, 1 finite, 2 infinity, 3 NaN)
 *   norm        B:A shifted up until bit 63 is set, C' = places shifted
 *   postnormsn  with W[0] = exponent and C = norm's shift: the exponent of
 *               the result, or the fraction shifted down to a denormal
 *   roundsn     pack B:A with exponent C, round to nearest even,
 *               overflowing to infinity
 *
 * so x = frac * 2^(exp - 158) on the way in, and B:A * 2^(W[0] - 190) is
 * packed on the way out by "norm; postnormsn; roundsn" with any sign ORed
 * in after.  This is how INMOS's REAL32 runtime for the T414 works
 * (t4/examples/sqrroots/sqrroots.b4h): multiply is lmul, divide is ldiv,
 * add aligns with lshr and adds or subtracts with sum/ldiff, all exact,
 * then that one rounding.
 *
 * Here: float add, subtract, multiply, divide and the relations; float to
 * and from int, unsigned and double; double to and from int and unsigned.
 * Each helper does the common case - finite operands, nonzero for the
 * arithmetic - and hands anything else on to the C in transputer-softfp.c
 * (__tp_addsf3, ...), which is also the reference these are checked
 * against bit for bit (tests/test_softfp_sn.c).  No cflerr: C wants IEEE
 * non-stop arithmetic, not occam's error flag.
 *
 * Layout: the assembler gives a branch to a label it has not seen yet a
 * nine-byte relocated form, so every conditional branch here goes back to
 * a block placed before its function, written `cj .Lx - .Lbase`, a
 * constant from the start of the section that encodes in one or two
 * bytes.  Each path ends in its own ret: a shared tail would need j,
 * which is a timeslice point.
 *
 * Built only into transputer-libtcc1-t425.a and -t414.a.  The ABI: the
 * arguments are W[1], W[2] on entry (a double takes two words, low
 * first), W[0] the return address; the result goes back in A, a double in
 * B:A.
 */

	.text
.Lbase:

/* ---------------------------------------------------------------- */
/* a * b                                                            */

.Lmul_c:
	ajw 4
	j __tp_mulsf3

	.globl __mulsf3
	.type __mulsf3, @function
__mulsf3:
	ajw -4			/* W1 fa, W2 ea, W3 fb; a = W5, b = W6 */
	ldc 0
	ldl 5
	unpacksn		/* A fa, B ea, C type a */
	stl 1
	stl 2
	ldl 6
	unpacksn		/* A fb, B eb, C 4 type a + type b */
	stl 3
	ldl 2
	add
	adc -126
	stl 0			/* W0 = ea + eb - 126 */
	eqc 5
	cj .Lmul_c - .Lbase	/* not both finite nonzero */
	ldc 0
	ldl 1
	ldl 3
	lmul			/* B:A = fa * fb, exact */
	norm
	postnormsn
	roundsn
	ldl 5
	ldl 6
	xor
	mint
	and
	or
	ajw 4
	ret

/* ---------------------------------------------------------------- */
/* a / b: (fa / 2 : 0) / fb, the remainder as the guard word.        */
/* Denormal operands go to the C too, so both fractions are          */
/* normalised and the quotient fits.                                 */

.Ldiv_c:
	ajw 4
	j __tp_divsf3

	.globl __divsf3
	.type __divsf3, @function
__divsf3:
	ajw -4			/* W1 fa, W2 ea, W3 fb; a = W5, b = W6 */
	ldc 0
	ldl 5
	unpacksn
	stl 1
	stl 2
	ldl 6
	unpacksn
	stl 3
	ldl 2
	rev
	sub
	adc 127
	stl 0			/* W0 = ea - eb + 127 */
	eqc 5
	cj .Ldiv_c - .Lbase
	ldl 1
	ldl 3
	and
	mint
	and
	cj .Ldiv_c - .Lbase	/* a denormal */
	ldl 1
	ldc 1
	shr
	ldc 0
	ldl 3
	ldiv			/* A quotient, 31 or 32 bits; B remainder */
	rev
	norm
	postnormsn
	roundsn
	ldl 5
	ldl 6
	xor
	mint
	and
	or
	ajw 4
	ret

/* ---------------------------------------------------------------- */
/* a + b and a - b.  With |x| >= |y| and d = ex - ey < 26, y's       */
/* fraction shifted down d + 1 places loses nothing in two words,    */
/* and so does x's shifted down one: their sum or difference is      */
/* exact and below 2^64.  From d = 26 on, |y| is under a quarter of  */
/* x's last place and the result is x.                               */
/* Workspace: W0 exponent, W1 fa, W2 ea, W3 fb, W4 eb; a W6, b W7.   */

.Ladd_c:			/* NaN, infinity or zero */
	ajw 5
	j __tp_addsf3

.Ladd_0:			/* x + -x = +0 */
	ldc 0
	ajw 5
	ret

.Ladd_a:
	ldl 6
	ajw 5
	ret

.Ladd_b:
	ldl 7
	ajw 5
	ret

.Ladd_asame:			/* |a| > |b|, same signs */
	ldl 1
	ldc 1
	shr
	ldl 4
	sum
	ldl 3
	norm
	postnormsn
	roundsn
	ldl 6
	mint
	and
	or
	ajw 5
	ret

.Ladd_bsame:			/* |b| >= |a|, same signs */
	ldl 3
	ldc 1
	shr
	ldl 2
	sum
	ldl 1
	norm
	postnormsn
	roundsn
	ldl 7
	mint
	and
	or
	ajw 5
	ret

.Ladd_bbig:			/* |b| >= |a| */
	ldl 4
	ldl 2
	sub
	stl 2			/* W2 = d = eb - ea */
	ldc 26
	ldl 2
	gt
	cj .Ladd_b - .Lbase
	ldl 4
	adc 1
	stl 0			/* W0 = eb + 1 */
	ldl 1
	ldc 0
	ldl 2
	adc 1
	lshr			/* (fa : 0) >> d + 1 */
	stl 1			/* W1 low */
	stl 2			/* W2 high */
	ldl 6
	ldl 7
	xor
	mint
	and
	cj .Ladd_bsame - .Lbase
	ldc 0
	ldc 0
	ldl 1
	ldiff
	stl 1			/* W1 = 0 - low; A borrow */
	ldl 3
	ldc 1
	shr
	ldl 2
	ldiff			/* fb / 2 - high - borrow */
	stl 2
	ldl 2
	cj .Ladd_0 - .Lbase	/* zero only when a = -b */
	ldl 2
	ldl 1
	norm
	postnormsn
	roundsn
	ldl 7
	mint
	and
	or
	ajw 5
	ret

	.globl __subsf3
	.type __subsf3, @function
__subsf3:
	ldl 2
	mint
	xor
	stl 2			/* a + -b, from here on and in the C */

	.globl __addsf3
	.type __addsf3, @function
__addsf3:
	ajw -5
	ldc 0
	ldl 6
	unpacksn
	stl 1
	stl 2
	ldl 7
	unpacksn
	stl 3
	stl 4
	eqc 5
	cj .Ladd_c - .Lbase
	ldl 6
	mint
	not
	and
	ldl 7
	mint
	not
	and
	gt
	cj .Ladd_bbig - .Lbase	/* not |a| > |b| */
	ldl 2
	ldl 4
	sub
	stl 4			/* W4 = d = ea - eb */
	ldc 26
	ldl 4
	gt
	cj .Ladd_a - .Lbase
	ldl 2
	adc 1
	stl 0			/* W0 = ea + 1 */
	ldl 3
	ldc 0
	ldl 4
	adc 1
	lshr			/* (fb : 0) >> d + 1 */
	stl 3			/* W3 low */
	stl 4			/* W4 high */
	ldl 6
	ldl 7
	xor
	mint
	and
	cj .Ladd_asame - .Lbase
	ldc 0
	ldc 0
	ldl 3
	ldiff
	stl 3			/* W3 = 0 - low; A borrow */
	ldl 1
	ldc 1
	shr
	ldl 4
	ldiff			/* fa / 2 - high - borrow: not 0, |a| > |b| */
	ldl 3
	norm
	postnormsn
	roundsn
	ldl 6
	mint
	and
	or
	ajw 5
	ret

/* ---------------------------------------------------------------- */
/* int and unsigned to float: i * 2^0 is i:0 * 2^(158 - 190)         */

.Lflt_0:
	ldc 0
	ret

.Lflt_neg:
	ajw -1
	ldc 158
	stl 0
	ldc 0
	ldl 2
	diff			/* -i as unsigned: MinInt is 2^31 */
	ldc 0
	norm
	postnormsn
	roundsn
	mint
	or
	ajw 1
	ret

	.globl __floatsisf
	.type __floatsisf, @function
__floatsisf:
	ldl 1
	cj .Lflt_0 - .Lbase
	ldl 1
	ldc 0
	gt
	cj .Lflt_neg - .Lbase
	ajw -1
	ldc 158
	stl 0
	ldl 2
	ldc 0
	norm
	postnormsn
	roundsn
	ajw 1
	ret

	.globl __floatunsisf
	.type __floatunsisf, @function
__floatunsisf:
	ldl 1
	cj .Lflt_0 - .Lbase
	ajw -1
	ldc 158
	stl 0
	ldl 2
	ldc 0
	norm
	postnormsn
	roundsn
	ajw 1
	ret

/* ---------------------------------------------------------------- */
/* float to int and unsigned, truncating: frac >> (158 - exp).       */
/* Out of range, infinity and NaN go to the C, which saturates.      */
/* Workspace: W0 frac, W1 s = 158 - exp; a = W3.                     */

.Lfix_c:
	ajw 2
	j __tp_fixsfsi

.Lfixu_c:
	ajw 2
	j __tp_fixunssfsi

.Lfix_0:
	ldc 0
	ajw 2
	ret

.Lfix_neg:
	ldc 0
	ldl 0
	ldl 1
	shr
	diff
	ajw 2
	ret

	.globl __fixsfsi
	.type __fixsfsi, @function
__fixsfsi:
	ajw -2
	ldc 0
	ldl 3
	unpacksn
	stl 0
	ldc 158
	rev
	diff
	stl 1
	ldc 32
	ldl 1
	gt
	cj .Lfix_0 - .Lbase	/* |a| < 1, zeros and denormals too */
	ldl 1
	ldc 0
	gt
	cj .Lfix_c - .Lbase	/* |a| >= 2^31, infinity, NaN */
	ldl 3
	ldc 0
	gt
	cj .Lfix_neg - .Lbase
	ldl 0
	ldl 1
	shr
	ajw 2
	ret

	.globl __fixunssfsi
	.type __fixunssfsi, @function
__fixunssfsi:
	ajw -2
	ldc 0
	ldl 3
	unpacksn
	stl 0
	ldc 158
	rev
	diff
	stl 1
	ldl 3
	ldc 0
	gt
	cj .Lfix_0 - .Lbase	/* negative, a zero or a NaN with the sign */
	ldc 32
	ldl 1
	gt
	cj .Lfix_0 - .Lbase	/* a < 1 */
	ldl 1
	ldc -1
	gt
	cj .Lfixu_c - .Lbase	/* a >= 2^32, infinity, NaN */
	ldl 0
	ldl 1
	shr
	ajw 2
	ret

/* ---------------------------------------------------------------- */
/* Relations: -1, 0 or 1 as a <, == or > b, or `unordered` when      */
/* either is a NaN (|x| > infinity): 1 for ==, !=, <, <=, -1 for >,  */
/* >=.  Each operand becomes a key in two's complement order, both   */
/* zeros 0: (|x| ^ m) - m, m the sign spread over a word by xdble.   */
/* Only the call's own W[1..3].                                      */

.Lcmp_ord:
	ldl 1
	xdble
	rev
	stl 3
	mint
	not
	and
	ldl 3
	xor
	ldl 3
	diff
	stl 1			/* key a */
	ldl 2
	xdble
	rev
	stl 3
	mint
	not
	and
	ldl 3
	xor
	ldl 3
	diff
	stl 2			/* key b */
	ldl 1
	ldl 2
	gt
	ldl 2
	ldl 1
	gt
	sub			/* (ka > kb) - (kb > ka) */
	ret

	.globl __eqsf2
	.type __eqsf2, @function
	.globl __nesf2
	.type __nesf2, @function
	.globl __ltsf2
	.type __ltsf2, @function
	.globl __lesf2
	.type __lesf2, @function
__eqsf2:
__nesf2:
__ltsf2:
__lesf2:
	ldl 1
	mint
	not
	and
	ldinf
	gt
	ldl 2
	mint
	not
	and
	ldinf
	gt
	or
	cj .Lcmp_ord - .Lbase
	ldc 1
	ret

	.globl __gtsf2
	.type __gtsf2, @function
	.globl __gesf2
	.type __gesf2, @function
__gtsf2:
__gesf2:
	ldl 1
	mint
	not
	and
	ldinf
	gt
	ldl 2
	mint
	not
	and
	ldinf
	gt
	or
	cj .Lcmp_ord - .Lbase
	ldc -1
	ret

	.globl __unordsf2
	.type __unordsf2, @function
__unordsf2:
	ldl 1
	mint
	not
	and
	ldinf
	gt
	ldl 2
	mint
	not
	and
	ldinf
	gt
	or
	ret

/* ---------------------------------------------------------------- */
/* float to double, exactly: the fraction from unpacksn normalised   */
/* by norm (a denormal too), hi = sign | (e - 1 + 896) << 20 plus    */
/* the fraction's top 21 bits, whose leading 1 carries the exponent  */
/* up by one; lo its next 3 bits.  A double comes back in B:A.       */

.Lext_0:
	ldc 0
	ldl 1
	mint
	and
	rev			/* A 0, B the sign */
	ret

.Lext_other:			/* a zero, an infinity or a NaN */
	ldl 1
	mint
	not
	and
	cj .Lext_0 - .Lbase
	j __tp_extendsfdf2

	.globl __extendsfdf2
	.type __extendsfdf2, @function
__extendsfdf2:
	ldc 0
	ldl 1
	unpacksn		/* A f, B e, C type */
	stl 2
	stl 3
	eqc 1
	cj .Lext_other - .Lbase
	ldl 2
	ldc 0
	norm			/* A 0, B f << n, C n */
	stl 2
	stl 2			/* W2 = f << n; A n */
	ldl 3
	rev
	sub
	adc 895
	ldc 20
	shl
	ldl 2
	ldc 11
	shr
	sum
	ldl 1
	mint
	and
	or			/* hi */
	ldl 2
	ldc 21
	shl			/* lo */
	ret

/* ---------------------------------------------------------------- */
/* double to float: the 53-bit significand shifted up to bit 63 of   */
/* B:A, W[0] = e - 896, then the same packing as the arithmetic,     */
/* which rounds once, overflows to infinity and makes denormals.     */
/* A double's zeros and denormals all round to a signed 0.           */

.Ltrunc_c:
	ajw 1
	j __tp_truncdfsf2

.Ltrunc_0:
	ldl 3
	mint
	and
	ajw 1
	ret

	.globl __truncdfsf2
	.type __truncdfsf2, @function
__truncdfsf2:
	ajw -1			/* lo W2, hi W3 */
	ldl 3
	ldc 20
	shr
	ldc 0x7ff
	and
	adc -896
	stl 0			/* W0 = e - 896 */
	ldl 0
	adc 896
	cj .Ltrunc_0 - .Lbase
	ldl 0
	eqc 0x7ff - 896
	eqc 0
	cj .Ltrunc_c - .Lbase	/* infinity or NaN */
	ldl 3
	ldl 2
	ldc 11
	lshl			/* A lo, B hi; the exponent's low bit at 63 */
	rev
	mint
	or
	rev
	norm
	postnormsn
	roundsn
	ldl 3
	mint
	and
	or
	ajw 1
	ret

/* ---------------------------------------------------------------- */
/* int and unsigned to double, exactly: |i| normalised, its leading  */
/* 1 at bit 31 carrying the exponent 1054 - n - 1 up by one.         */

.Lfltd_0:
	ldc 0
	ldc 0
	ret

	.globl __floatsidf
	.type __floatsidf, @function
__floatsidf:
	ldl 1
	cj .Lfltd_0 - .Lbase
	ldl 1
	mint
	and
	stl 2			/* W2 sign */
	ldl 1
	xdble
	rev
	stl 3
	ldl 3
	xor
	ldl 3
	diff			/* |i|, MinInt as 2^31 */
	ldc 0
	norm
	stl 3
	stl 1			/* W1 = |i| << n; A n */
	ldc 1053
	rev
	diff
	ldc 20
	shl
	ldl 1
	ldc 11
	shr
	sum
	ldl 2
	or			/* hi */
	ldl 1
	ldc 21
	shl			/* lo */
	ret

	.globl __floatunsidf
	.type __floatunsidf, @function
__floatunsidf:
	ldl 1
	cj .Lfltd_0 - .Lbase
	ldl 1
	ldc 0
	norm
	stl 3
	stl 1
	ldc 1053
	rev
	diff
	ldc 20
	shl
	ldl 1
	ldc 11
	shr
	sum
	ldl 1
	ldc 21
	shl
	ret

/* ---------------------------------------------------------------- */
/* double to int and unsigned, truncating: the significand's top 32  */
/* bits >> (1054 - e).  lo W1, hi W2, W3 the shift.                  */

.Lfixd_c:
	j __tp_fixdfsi

.Lfixud_c:
	j __tp_fixunsdfsi

.Lfixd_0:
	ldc 0
	ret

.Lfixd_neg:
	ldc 0
	ldl 1
	diff
	ret

	.globl __fixdfsi
	.type __fixdfsi, @function
__fixdfsi:
	ldl 2
	ldc 20
	shr
	ldc 0x7ff
	and
	ldc 1054
	rev
	diff
	stl 3			/* s = 1054 - e */
	ldc 32
	ldl 3
	gt
	cj .Lfixd_0 - .Lbase	/* |a| < 1 */
	ldl 3
	ldc 0
	gt
	cj .Lfixd_c - .Lbase	/* |a| >= 2^31, infinity, NaN */
	ldl 2
	ldl 1
	ldc 11
	lshl
	rev
	mint
	or
	ldl 3
	shr
	stl 1
	ldl 2
	ldc 0
	gt
	cj .Lfixd_neg - .Lbase
	ldl 1
	ret

	.globl __fixunsdfsi
	.type __fixunsdfsi, @function
__fixunsdfsi:
	ldl 2
	ldc 0
	gt
	cj .Lfixd_0 - .Lbase	/* negative, or below 2^-1022 */
	ldl 2
	ldc 20
	shr
	ldc 1054
	rev
	diff
	stl 3
	ldc 32
	ldl 3
	gt
	cj .Lfixd_0 - .Lbase	/* a < 1 */
	ldl 3
	ldc -1
	gt
	cj .Lfixud_c - .Lbase	/* a >= 2^32, infinity, NaN */
	ldl 2
	ldl 1
	ldc 11
	lshl
	rev
	mint
	or
	ldl 3
	shr
	ret
