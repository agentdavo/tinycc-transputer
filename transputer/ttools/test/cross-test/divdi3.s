.globl ___udivmoddi4;
.text
.globl ___divdi3;
___divdi3:
	ajw -31
	ldl 32
	stl 2
	ldl 33
	stl 3
	ldl 34
	ldl 35
	ldc 0
	stl 4
	ldl 2
	stl 5
	ldl 3
	stl 6
	rev
	stl 7
	stl 8
	ldl 4
	ldl 6
	gt
	cj L@2
	ldc -1
	stl 4
	ldl 5
	stl 13
	ldl 3
	stl 14
	ldl 13
	not
	adc 1
	stl 15
	ldl 14
	not
	adc 1
	ldl 15
	eqc 0
	eqc 0
	diff
	dup
	stl 16
	ldl 15
	stl 11
	stl 12
	j L@4
L@4:
	ldl 11
	stl 5
	ldl 12
	stl 6
L@2:
	ldc 0
	ldl 8
	gt
	cj L@5
	ldl 4
	not
	stl 4
	ldl 7
	stl 19
	ldl 8
	stl 20
	ldl 19
	not
	adc 1
	stl 21
	ldl 20
	not
	adc 1
	ldl 21
	eqc 0
	eqc 0
	diff
	dup
	stl 22
	ldl 21
	stl 17
	stl 18
	j L@7
L@7:
	ldl 17
	stl 7
	ldl 18
	stl 8
L@5:
	ldc 0
	stl 1
	ldl 8
	stl 0
	ldl 6
	ldl 7
	rev
	ldl 5
	call ___udivmoddi4
	stl 9
	stl 10
	ldl 4
	cj L@8
	ldl 9
	stl 23
	ldl 10
	stl 24
	ldl 23
	stl 25
	ldl 10
	stl 26
	ldl 25
	stl 27
	ldl 10
	stl 28
	ldl 27
	not
	adc 1
	stl 29
	ldl 28
	not
	adc 1
	ldl 29
	eqc 0
	eqc 0
	diff
	dup
	stl 30
	ldl 29
	stl 9
	stl 10
	j L@8
L@8:
	ldl 10
	ldl 9
	ajw +31
	ret
