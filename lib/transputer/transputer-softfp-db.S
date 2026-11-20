/*
 * transputer-softfp-db.S - double-length add, subtract, multiply,
 * divide and relations for the T414/T425, on the long arithmetic
 * instructions.
 *
 * The FP-support instructions are single-length only (see
 * transputer-softfp-sn.S), but the method carries over: unpack both
 * operands into 64-bit fractions with the leading 1 at bit 63 (or 62),
 * compute the exact result with lmul, ldiv, lshr, lsum and ldiff, then
 * round once.  The rounding is PACK below, to nearest even by hand: norm
 * puts the result's leading 1 at bit 63 of B:A, the low 11 bits and a
 * sticky word decide the increment, and the exponent, less one, is added
 * to the high word under the fraction's leading 1, so a carry out of the
 * fraction steps the exponent, up to infinity.
 *
 *   multiply  the 128-bit product of the two fractions from four lmul;
 *             its top half, normalised, rounds, the rest is sticky
 *   divide    Knuth's algorithm D with 32-bit digits: (A >> s : 0) / B,
 *             s making the dividend's fraction smaller than the divisor's,
 *             two quotient digits estimated by ldiv on the top words and
 *             corrected by adding the divisor back, at most twice each;
 *             the final remainder is the sticky bit
 *   add       the transputer-softfp.c algorithm: fractions at bit 62, the
 *             smaller one shifted right with every bit lost ORed into bit
 *             0, then summed or subtracted, exact up to that bit
 *
 * Finite nonzero normal operands whose result is a normal number stay
 * here; anything else - NaN, infinity, zero or denormal operands, a
 * result that overflows or underflows, an exact zero difference, a
 * quotient digit ldiv cannot estimate - is redone by the C (__tp_adddf3,
 * ...), which is also the reference these are checked against bit for
 * bit (tests/test_softfp_sn.c).  So the C is the specification, and this
 * only has to be right where it answers.
 *
 * As in transputer-softfp-sn.S, every conditional branch on a hot path
 * goes back to a block placed before its function, `cj .Lx - .Lbase`;
 * the blocks that rejoin the main path do so with a forward cj, which
 * the assembler relocates in nine bytes.
 *
 * The ABI: a = W[1] (low), W[2] (high) and b = W[3], W[4] on entry, W[0]
 * the return address; the result comes back in B:A.  Each function's
 * frame is 11 words, so a is W12, W13 and b W14, W15 after its ajw.
 */

/* Round and pack: rh:rl the result with its leading 1 at bit 63, s a
   word that is nonzero when anything below rl was lost (also the scratch
   slot for the increment), e the biased exponent, 1..2046, and sign the
   instructions that leave the sign bit in A.  Leaves the double in B:A. */
#define PACK(rh, rl, s, e, sign) \
	ldl s; eqc 0; eqc 0; \
	ldl rl; ldc 0x7ff; and; or; \
	ldl rl; ldc 0x800; and; eqc 0; eqc 0; add; \
	ldc 0x400; gt; stl s; \
	ldl rh; ldl rl; ldc 11; lshr; stl rl; stl rh; \
	ldc 0; ldl rl; ldl s; lsum; stl rl; \
	ldl rh; sum; \
	ldl e; adc -1; ldc 20; shl; sum; \
	sign; or; \
	ldl rl

/* A double's biased exponent, from its high word in slot hi */
#define EXP(hi) \
	ldl hi; ldc 1; shl; ldc 21; shr

/* 1 when the biased exponent in slot e is 1..2046 */
#define NORMAL(e) \
	ldl e; adc 1; ldc 2047; and; ldc 1; gt

/* 1 when e is 1..2046, for an exponent that may be anything */
#define INRANGE(e) \
	ldl e; ldc 0; gt; ldc 2047; ldl e; gt; and

/* A double's fraction with its leading 1 at bit 63: A low, B high */
#define FRAC63(hi, lo) \
	ldl hi; ldl lo; ldc 11; lshl; rev; mint; or; rev

	.text
.Lbase:

/* ---------------------------------------------------------------- */
/* a * b.  Workspace: W1 ea then e, W2 eb, W3:W4 fraction a, W5:W6    */
/* fraction b, W0 P0 then the sticky word, W7..W10 partial products. */

.Lmul_c:
	ajw 11
	j __tp_muldf3

	.globl __muldf3
	.type __muldf3, @function
__muldf3:
	ajw -11
	EXP(13)
	stl 1
	EXP(15)
	stl 2
	NORMAL(1)
	NORMAL(2)
	and
	cj .Lmul_c - .Lbase
	FRAC63(13, 12)
	stl 3
	stl 4
	FRAC63(15, 14)
	stl 5
	stl 6
	ldc 0
	ldl 3
	ldl 5
	lmul			/* a0 b0: A P0, B carry */
	stl 0
	stl 7
	ldl 7
	ldl 4
	ldl 5
	lmul			/* a1 b0 + carry */
	stl 7
	stl 8
	ldl 7
	ldl 3
	ldl 6
	lmul			/* a0 b1 + low of that: A P1 */
	ldl 0
	or
	stl 0			/* W0 = P1 | P0, the sticky word */
	stl 7
	ldl 8
	ldl 4
	ldl 6
	lmul			/* a1 b1 + high of a1 b0 */
	stl 8
	stl 9
	ldc 0
	ldl 8
	ldl 7
	lsum			/* + high of a0 b1: A P2, B carry */
	stl 8
	ldl 9
	sum			/* P3 */
	ldl 8
	norm			/* P3:P2 up to bit 63, C 0 or 1 */
	stl 9
	stl 10
	ldl 1
	rev
	sub
	ldl 2
	add
	adc -1022
	stl 1			/* e = ea + eb - 1022 - n */
	INRANGE(1)
	cj .Lmul_c - .Lbase
	PACK(10, 9, 0, 1, ldl 13; ldl 15; xor; mint; and)
	ajw 11
	ret

/* ---------------------------------------------------------------- */
/* a / b.  Workspace: W1 ea then e, W2 eb then scratch, W3:W4 the      */
/* dividend's fraction, then each partial remainder, W5:W6 the         */
/* divisor's fraction (v0, v1), W7 s then the second digit, W8 the     */
/* first digit, W0, W9, W10 scratch.  A remainder is W4:W3:W0 after    */
/* the first digit and W3:W0:W2 after the second; it is negative       */
/* exactly when its top word is not 0.                                 */

.Ldiv_c:
	ajw 11
	j __tp_divdf3

.Ldiv_fix1:			/* add the divisor back to W4:W3:W0 */
	ldl 8
	ldc 1
	diff
	stl 8
	ldc 0
	ldl 0
	ldl 5
	lsum
	stl 0
	ldl 3
	ldl 6
	lsum
	stl 3
	ldl 4
	sum
	stl 4
	ldl 4
	eqc 0
	cj .Ldiv_fix1 - .Lbase
	ldc 0
	cj .Ldiv_d2

.Ldiv_fix2:			/* and to W3:W0:W2 */
	ldl 7
	ldc 1
	diff
	stl 7
	ldc 0
	ldl 2
	ldl 5
	lsum
	stl 2
	ldl 0
	ldl 6
	lsum
	stl 0
	ldl 3
	sum
	stl 3
	ldl 3
	eqc 0
	cj .Ldiv_fix2 - .Lbase
	ldc 0
	cj .Ldiv_d3

	.globl __divdf3
	.type __divdf3, @function
__divdf3:
	ajw -11
	EXP(13)
	stl 1
	EXP(15)
	stl 2
	NORMAL(1)
	NORMAL(2)
	and
	cj .Ldiv_c - .Lbase
	FRAC63(13, 12)
	stl 3
	stl 4
	FRAC63(15, 14)
	stl 5
	stl 6
	ldc 0
	ldl 3
	ldl 5
	ldiff
	stl 0
	ldl 4
	ldl 6
	ldiff
	stl 0
	eqc 0
	stl 7			/* s = fraction a >= fraction b */
	ldl 4
	ldl 3
	ldl 7
	lshr
	stl 3
	stl 4			/* W4:W3 = fraction a >> s, below b */
	ldl 1
	ldl 2
	sub
	ldl 7
	add
	adc 1022
	stl 1			/* e = ea - eb + 1022 + s */
	INRANGE(1)
	cj .Ldiv_c - .Lbase
	/* first digit, of W4:W3:0 by W6:W5 */
	ldl 4
	ldl 6
	diff
	cj .Ldiv_c - .Lbase	/* top words equal: no ldiv estimate */
	ldl 4
	ldl 3
	ldl 6
	ldiv
	stl 8			/* q1, 0 to 2 too big */
	ldc 0
	ldl 8
	ldl 5
	lmul
	stl 0
	stl 9
	ldl 9
	ldl 8
	ldl 6
	lmul			/* q1 * b: W10:A:W0 */
	stl 9
	stl 10
	ldc 0
	ldc 0
	ldl 0
	ldiff
	stl 0
	ldl 3
	ldl 9
	ldiff
	stl 3
	ldl 4
	ldl 10
	ldiff
	stl 4			/* W4:W3:W0 = dividend - q1 * b */
	ldl 4
	eqc 0
	cj .Ldiv_fix1 - .Lbase
.Ldiv_d2:
	/* second digit, of W3:W0:0 */
	ldl 3
	ldl 6
	diff
	cj .Ldiv_c - .Lbase
	ldl 3
	ldl 0
	ldl 6
	ldiv
	stl 7			/* q2 */
	ldc 0
	ldl 7
	ldl 5
	lmul
	stl 2
	stl 9
	ldl 9
	ldl 7
	ldl 6
	lmul
	stl 9
	stl 10
	ldc 0
	ldc 0
	ldl 2
	ldiff
	stl 2
	ldl 0
	ldl 9
	ldiff
	stl 0
	ldl 3
	ldl 10
	ldiff
	stl 3			/* W3:W0:W2 = remainder - q2 * b */
	ldl 3
	eqc 0
	cj .Ldiv_fix2 - .Lbase
.Ldiv_d3:
	ldl 0
	ldl 2
	or
	stl 2			/* the remainder: sticky */
	PACK(8, 7, 2, 1, ldl 13; ldl 15; xor; mint; and)
	ajw 11
	ret

/* ---------------------------------------------------------------- */
/* a + b and a - b.  b's high word, its sign flipped for a - b, is    */
/* W9.  x is the operand of larger magnitude and y the other:         */
/* W1 ex then e, W2 ey then d = ex - ey, W3:W4 x's fraction and       */
/* W5:W6 y's, both with the leading 1 at bit 62, W7 x's sign, W10:W0  */
/* x itself, W8 the sticky word.                                      */

/* A double's fraction with its leading 1 at bit 62: A low, B high */
#define FRAC62(hi, lo) \
	FRAC63(hi, lo); ldc 1; lshr

.Ladd_c:			/* redo it in C, with b's sign as used */
	ldl 9
	stl 0
	ldl 14
	ldl 13
	ldl 12
	call __tp_adddf3
	ajw 11
	ret

.Ladd_x:			/* y is under a quarter of x's last place */
	ldl 0
	ldl 10
	ajw 11
	ret

.Ladd_swap:			/* |b| > |a| */
	FRAC62(9, 14)
	stl 3
	stl 4
	FRAC62(13, 12)
	stl 5
	stl 6
	ldl 9
	mint
	and
	stl 7
	ldl 9
	stl 0
	ldl 14
	stl 10
	ldl 1
	ldl 2
	stl 1
	stl 2
	ldc 0
	cj .Ladd_sel

.Ladd_w32:			/* d >= 32: shift y down a word first */
	ldl 5
	stl 8
	ldl 6
	stl 5
	ldc 0
	stl 6
	ldl 2
	adc -32
	stl 2
	ldc 0
	cj .Ladd_al

.Ladd_opp:			/* the signs differ: x - y */
	ldc 0
	ldl 3
	ldl 5
	ldiff
	stl 3
	ldl 4
	ldl 6
	ldiff
	stl 4
	ldl 4
	ldl 3
	or
	cj .Ladd_c - .Lbase	/* x + -x: C gives the +0 */
	ldl 4
	ldl 3
	norm
	stl 3
	stl 4
	ldl 1
	rev
	sub
	adc 1
	stl 1			/* e = ex + 1 - n */
	INRANGE(1)
	cj .Ladd_c - .Lbase
	ldc 0
	stl 8
	PACK(4, 3, 8, 1, ldl 7)
	ajw 11
	ret

	.globl __adddf3
	.type __adddf3, @function
__adddf3:
	ajw -11
	ldl 15
	stl 9
.Ladd_join:
	EXP(13)
	stl 1
	EXP(9)
	stl 2
	NORMAL(1)
	NORMAL(2)
	and
	cj .Ladd_c - .Lbase
	ldl 13
	mint
	not
	and
	stl 7
	ldl 9
	mint
	not
	and
	stl 8
	ldc 0
	ldl 12
	ldl 14
	ldiff
	stl 0
	ldl 7
	ldl 8
	ldiff
	stl 0			/* A: borrow out of |a| - |b| */
	eqc 0
	cj .Ladd_swap - .Lbase
	FRAC62(13, 12)
	stl 3
	stl 4
	FRAC62(9, 14)
	stl 5
	stl 6
	ldl 13
	mint
	and
	stl 7
	ldl 13
	stl 0
	ldl 12
	stl 10
.Ladd_sel:
	ldl 1
	ldl 2
	sub
	stl 2			/* d = ex - ey */
	ldc 61
	ldl 2
	gt
	cj .Ladd_x - .Lbase	/* d >= 61 */
	ldc 0
	stl 8
	ldc 32
	ldl 2
	gt
	cj .Ladd_w32 - .Lbase
.Ladd_al:
	ldc 1
	ldl 2
	shl
	ldc 1
	diff			/* (1 << d) - 1 */
	ldl 5
	and
	ldl 8
	or
	eqc 0
	eqc 0
	stl 8			/* anything of y below the shift */
	ldl 6
	ldl 5
	ldl 2
	lshr
	ldl 8
	or
	stl 5
	stl 6			/* y >> d, the lost bits ORed into bit 0 */
	ldl 13
	ldl 9
	xor
	mint
	and
	eqc 0
	cj .Ladd_opp - .Lbase
	ldc 0
	ldl 3
	ldl 5
	lsum
	stl 3
	ldl 4
	ldl 6
	lsum
	stl 4			/* x + y, below 2^64 */
	ldl 4
	ldl 3
	norm
	stl 3
	stl 4
	ldl 1
	rev
	sub
	adc 1
	stl 1
	INRANGE(1)
	cj .Ladd_c - .Lbase
	ldc 0
	stl 8
	PACK(4, 3, 8, 1, ldl 7)
	ajw 11
	ret

	.globl __subdf3
	.type __subdf3, @function
__subdf3:
	ajw -11
	ldl 15
	mint
	xor
	stl 9
	ldc 0
	cj .Ladd_join - .Lbase

/* ---------------------------------------------------------------- */
/* Relations: -1, 0 or 1 as a <, == or > b, or `unordered` (W3) when  */
/* either is a NaN: 1 for ==, !=, <, <=, -1 for >, >=.  x is a NaN    */
/* when (|hi| | (lo != 0)) is above infinity's #7FF00000.  Otherwise  */
/* the bit patterns compare as unsigned 64-bit numbers (ldiff's       */
/* borrow), and that order is reversed when either is negative: both  */
/* negative, the larger pattern is the smaller number; signs that     */
/* differ, the negative pattern is the larger.  Last, two zeros of    */
/* any sign are equal.  Workspace: W2 scratch, W3 unordered, W4 a's   */
/* NaN test then the result; a = W6, W7, b = W8, W9.                  */

#define ISNAN64(lo, hi) \
	ldl lo; eqc 0; eqc 0; ldl hi; mint; not; and; or; \
	ldc 0x7ff00000; gt

.Ldcmp_ord:
	ldc 0
	ldl 6
	ldl 8
	ldiff
	stl 2
	ldl 7
	ldl 9
	ldiff
	stl 2
	stl 4			/* a < b, as patterns */
	ldc 0
	ldl 8
	ldl 6
	ldiff
	stl 2
	ldl 9
	ldl 7
	ldiff
	stl 2			/* b < a */
	ldl 4
	sub
	stl 4
	ldl 7
	ldl 9
	or
	xdble
	rev
	stl 2			/* -1 when either is negative */
	ldl 4
	ldl 2
	xor
	ldl 2
	diff
	stl 4
	ldl 7
	ldl 9
	or
	ldc 1
	shl
	ldl 6
	or
	ldl 8
	or
	eqc 0
	eqc 0
	ldc 0
	rev
	diff			/* 0 when both are zeros, else -1 */
	ldl 4
	and
	ajw 5
	ret

.Ldcmp:
	ISNAN64(6, 7)
	stl 4
	ISNAN64(8, 9)
	ldl 4
	or
	cj .Ldcmp_ord - .Lbase
	ldl 3
	ajw 5
	ret

	.globl __eqdf2
	.type __eqdf2, @function
	.globl __nedf2
	.type __nedf2, @function
	.globl __ltdf2
	.type __ltdf2, @function
	.globl __ledf2
	.type __ledf2, @function
__eqdf2:
__nedf2:
__ltdf2:
__ledf2:
	ajw -5
	ldc 1
	stl 3
	ldc 0
	cj .Ldcmp - .Lbase

	.globl __gtdf2
	.type __gtdf2, @function
	.globl __gedf2
	.type __gedf2, @function
__gtdf2:
__gedf2:
	ajw -5
	ldc -1
	stl 3
	ldc 0
	cj .Ldcmp - .Lbase

	.globl __unorddf2
	.type __unorddf2, @function
__unorddf2:
	ajw -5
	ISNAN64(6, 7)
	stl 4
	ISNAN64(8, 9)
	ldl 4
	or
	ajw 5
	ret
