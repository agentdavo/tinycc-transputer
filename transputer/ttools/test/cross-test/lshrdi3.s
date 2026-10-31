.text
.globl ___lshrdi3;
___lshrdi3:
	ajw -8
	ldl 9
	stl 0
	ldl 10
	stl 1
	ldl 11
	eqc 0
	cj L@1
	ldl 1
	ldl 0
	ajw +8
	ret
L@1:
	ldl 0
	stl 5
	ldl 1
	stl 6
	ldc 32
	ldl 11
	diff
	stl 4
	ldl 4
	ldc 0
	gt
	eqc 0
	cj L@2
	ldc 0
	stl 3
	ldl 6
	ldl 4
	not
	adc 1
	shr
	stl 2
	j L@3
L@2:
	ldl 6
	ldl 4
	shl
	stl 7
	ldl 6
	ldl 11
	shr
	stl 3
	ldl 5
	ldl 11
	shr
	ldl 7
	or
	stl 2
L@3:
	ldl 3
	ldl 2
	ajw +8
	ret
