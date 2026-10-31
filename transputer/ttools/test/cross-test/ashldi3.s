.text
.globl ___ashldi3;
___ashldi3:
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
	stl 2
	ldl 5
	ldl 4
	not
	adc 1
	shl
	stl 3
	j L@3
L@2:
	ldl 5
	ldl 4
	shr
	stl 7
	ldl 5
	ldl 11
	shl
	stl 2
	ldl 6
	ldl 11
	shl
	ldl 7
	or
	stl 3
L@3:
	ldl 3
	ldl 2
	ajw +8
	ret
