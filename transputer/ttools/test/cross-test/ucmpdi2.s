.text
.globl ___ucmpdi2;
___ucmpdi2:
	ajw -6
	ldl 7
	ldl 8
	ldl 9
	stl 0
	ldl 10
	stl 1
	rev
	stl 2
	stl 3
	ldl 0
	stl 4
	ldl 1
	stl 5
	ldc 0
	ldl 3
	ldl 5
	ldiff
	rev
	cj L@1
	ldc 0
	ajw +6
	ret
L@1:
	ldc 0
	ldl 5
	ldl 3
	ldiff
	rev
	cj L@2
	ldc 2
	ajw +6
	ret
L@2:
	ldc 0
	ldl 2
	ldl 4
	ldiff
	rev
	cj L@4
	ldc 0
	ajw +6
	ret
L@4:
	ldc 0
	ldl 4
	ldl 2
	ldiff
	rev
	cj L@5
	ldc 2
	ajw +6
	ret
L@5:
	ldc 1
	ajw +6
	ret
