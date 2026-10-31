.globl ___udivmoddi4;
.text
.globl ___moddi3;
___moddi3:
	ajw -31
	ldl 32
	stl 4
	ldl 33
	stl 5
	ldl 34
	ldl 35
	ldc 0
	stl 6
	ldl 4
	stl 7
	ldl 5
	stl 8
	rev
	stl 9
	stl 10
	ldl 6
	ldl 8
	gt
	cj L@2
	ldc -1
	stl 6
	ldl 7
	stl 13
	ldl 5
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
	stl 7
	ldl 12
	stl 8
L@2:
	ldc 0
	ldl 10
	gt
	cj L@5
	ldl 9
	stl 19
	ldl 10
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
	stl 9
	ldl 18
	stl 10
L@5:
	ldlp 2
	stl 1
	ldl 10
	stl 0
	ldl 8
	ldl 9
	rev
	ldl 7
	call ___udivmoddi4
	ldl 6
	cj L@8
	ldl 2
	stl 23
	ldl 3
	stl 24
	ldl 23
	stl 27
	ldl 24
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
	stl 25
	stl 26
	j L@10
L@10:
	ldl 25
	stl 2
	ldl 26
	stl 3
L@8:
	ldl 3
	ldl 2
	ajw +31
	ret
