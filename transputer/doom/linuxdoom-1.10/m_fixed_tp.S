/*
 * m_fixed_tp.S - FixedMul and FixedDiv for the transputer.
 *
 * m_fixed.c's FixedMul is a 64-bit multiply in C, which TinyCC builds as a
 * full 64 x 64 product (two sign words made by 31-place long shifts, two
 * prods and an lmul, then the shift by 16 done in halves): about 390 T800
 * cycles a call.  Its FixedDiv2 divides in double: FPU work on the T800,
 * T805 and T9000, and five soft-float calls on the T425 and T414.  Both
 * are one instruction here.
 *
 * FixedMul: lmul gives the unsigned product of a and b; the signed one
 * has (a < 0 ? b : 0) + (b < 0 ? a : 0) less in its high word.  The
 * result is bits 16..47 of the product, which is the low word lshr 16
 * leaves.  Exact, so it returns what the C does for every input.
 *
 * FixedDiv: ldiv divides (|a| << 16) by |b|, the sign is put back after,
 * truncating toward zero.  This is DOS DOOM's FixedDiv (an idiv), which
 * linuxdoom replaced by the double version; the two can differ in the
 * last bit when a / b rounds up to the next multiple of 2^-16 in double.
 * The overflow test is m_fixed.c's: (abs(a) >> 14) >= abs(b) returns
 * MININT or MAXINT by the sign of a ^ b, compared signed as the C does
 * (abs(MININT) is MININT there too).  FixedDiv2 is the same entry: it is
 * only ever reached through FixedDiv, and saturates where the double
 * version called I_Error.
 *
 * Only lmul, ldiv, long shifts, xdble and the ALU, so the same code runs
 * on every part from the T414 to the T9000.  The ABI: arguments in W[1],
 * W[2] on entry, W[0] the return address, W[3] (the call's C) free; the
 * result goes back in A.  The one conditional branch goes back to a block
 * before its function (see lib/transputer/transputer-softfp-sn.S).
 */

	.text
.Lbase:

	.globl FixedMul
	.type FixedMul, @function
FixedMul:			/* a = W1, b = W2 */
	ldc 0
	ldl 1
	gt
	ldc 0
	rev
	diff			/* -(a < 0) */
	ldl 2
	and			/* a < 0 ? b : 0 */
	ldc 0
	ldl 2
	gt
	ldc 0
	rev
	diff			/* -(b < 0) */
	ldl 1
	and			/* b < 0 ? a : 0 */
	sum
	stl 3			/* W3 = what the high word has too much */
	ldc 0
	ldl 1
	ldl 2
	lmul			/* A low, B high, unsigned a * b */
	rev
	ldl 3
	diff
	rev			/* A low, B high, signed */
	ldc 16
	lshr			/* A = bits 16..47 */
	ret

/* (abs(a) >> 14) >= abs(b): MAXINT, or MININT when a ^ b < 0 */
.Lovf:
	ldl 2
	mint
	not
	sum
	ajw 3
	ret

	.globl FixedDiv
	.type FixedDiv, @function
	.globl FixedDiv2
	.type FixedDiv2, @function
FixedDiv:
FixedDiv2:
	ajw -3			/* W0 |a|, W1 |b|, W2 sign; a = W4, b = W5 */
	ldl 4
	xdble
	xor
	ldc 0
	ldl 4
	gt
	sum
	stl 0			/* |a| = (a ^ s) + (a < 0) */
	ldl 5
	xdble
	xor
	ldc 0
	ldl 5
	gt
	sum
	stl 1			/* |b| */
	ldc 0
	ldl 4
	ldl 5
	xor
	gt
	stl 2			/* W2 = (a ^ b) < 0 */
	ldl 1
	ldl 0
	ldc 14
	shr
	gt
	cj .Lovf - .Lbase	/* not |b| > |a| >> 14 */
	ldc 0
	ldl 0
	ldc 16
	lshl			/* B:A = |a| << 16, and B < |b| */
	ldl 1
	ldiv			/* A = quotient */
	ldc 0
	ldl 2
	diff
	xor
	ldl 2
	sum			/* negated when the signs differ */
	ajw 3
	ret
