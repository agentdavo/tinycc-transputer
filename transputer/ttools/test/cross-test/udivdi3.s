.globl ___udivmoddi4;
.text
.globl ___udivdi3;
___udivdi3:
	ajw -8
	ldl 9
	stl 2
	ldl 10
	stl 3
	ldl 11
	stl 4
	ldl 12
	stl 5
	ldc 0
	stl 1
	ldl 5
	stl 0
	ldl 3
	ldl 4
	rev
	ldl 2
	call ___udivmoddi4
	stl 6
	stl 7
	ldl 7
	ldl 6
	ajw +8
	ret
