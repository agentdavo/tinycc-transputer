.text
.globl ___ashrdi3;
___ashrdi3:
	ajw -15
	ldl 16
	stl 0
	ldl 17
	stl 1
	ldl 18
	eqc 0
	cj L@1
	ldl 1
	ldl 0
	ajw +15
	ret
L@1:
	ldl 0
	stl 5
	ldl 1
	stl 6
	ldc 32
	ldl 18
	diff
	stl 4
	ldl 4
	ldc 0
	gt
	stl 14
	ldl 14
	eqc 0
	cj L@2
	ldl 1
	xdble
	stl 7
	stl 8
	ldl 8
	ldl 7
	ldc 31
	lshr
	stl 3
	ldl 8
	ldl 7
	ldl 4
	not
	adc 1
	lshr
	stl 2
	j L@3
L@2:
	ldl 6
	ldl 4
	shl
	stl 9
	ldl 1
	xdble
	stl 12
	stl 13
	ldl 13
	ldl 12
	ldl 18
	lshr
	stl 10
	stl 11
	ldl 10
	stl 3
	ldl 5
	ldl 18
	shr
	stl 14
	ldl 9
	ldl 14
	or
	stl 2
L@3:
	ldl 3
	ldl 2
	ajw +15
	ret
