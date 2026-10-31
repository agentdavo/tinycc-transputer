.globl ___udivmoddi4;
.text
.globl ___umoddi3;
___umoddi3:
	ajw -8
	ldl 9
	stl 4
	ldl 10
	stl 5
	ldl 11
	stl 6
	ldl 12
	stl 7
	ldlp 2
	stl 1
	ldl 7
	stl 0
	ldl 5
	ldl 6
	rev
	ldl 4
	call ___udivmoddi4
	ldl 3
	ldl 2
	ajw +8
	ret
