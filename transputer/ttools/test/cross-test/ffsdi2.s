.globl _ffs;
.text
.globl ___ffsdi2;
___ffsdi2:
	ajw -13
	ldl 14
	stl 0
	ldl 15
	stl 1
	ldl 0
	stl 2
	ldl 1
	stl 3
	ldc 0
	stl 5
	ldl 2
	stl 7
	ldc 0
	stl 8
	ldl 8
	ldl 7
	bitrevword
	rev
	bitrevword
	norm
	diff
	rev
	adc 1
	ldc 65
	rem
	stl 6
	ldl 6
	stl 4
	ldl 4
	cj L@1
	ldl 5
	ldl 6
	ajw +13
	ret
L@1:
	ldl 1
	stl 10
	ldc 0
	stl 11
	ldl 11
	ldl 10
	bitrevword
	rev
	bitrevword
	norm
	diff
	rev
	adc 1
	ldc 65
	rem
	stl 9
	ldl 9
	stl 4
	ldl 4
	cj L@2
	ldl 9
	ldnlp 8
	stl 12
	ldl 12
	stl 4
	ldl 12
	ldl 5
	dup
	rev
	ajw +13
	ret
L@2:
	ldl 5
	ldc 0
	ajw +13
	ret
