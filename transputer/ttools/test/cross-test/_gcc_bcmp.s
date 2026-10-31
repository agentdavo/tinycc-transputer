.text
.globl ___gcc_bcmp;
___gcc_bcmp:
	ajw -1
L@1:
	ldl 4
	eqc 0
	cj L@3
	j L@2
L@3:
	ldl 2
	lb
	ldl 2
	adc 1
	stl 2
	ldl 3
	lb
	ldl 3
	adc 1
	stl 3
	diff
	stl 0
	ldl 0
	eqc 0
	cj L@5
	ldl 4
	adc -1
	stl 4
	j L@1
L@2:
	ldc 0
	ajw +1
	ret
L@5:
	ldl 0
	ajw +1
	ret
