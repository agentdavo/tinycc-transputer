.globl ___fixunsdfdi;
.globl ___negdi2;
.text
	.align 4;
LC@0:
	.word 0x0, 0x0	// double 0
.globl ___fixdfdi;
___fixdfdi:
	ajw -8
	ldlp 9
	fpldnldb
	ldlp 9
	fpstnldb
	ldc LC@0-LF10
	ldpi
LF10:
	fpldnldb
	ldlp 9
	fpldnldb
	fpgt
	cj L@1
	ldc LC@0-LF17
	ldpi
LF17:
	fpldnldb
	ldlp 9
	fpldnldb
	fpsub
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call ___fixunsdfdi
	stl 2
	stl 3
	ldl 3
	ldl 2
	call ___negdi2
	stl 4
	stl 5
	ldl 5
	ldl 4
	ajw +8
	ret
L@1:
	ldlp 0
	ldlp 9
	fpldnldb
	fpstnldb
	ldl 1
	ldl 0
	call ___fixunsdfdi
	stl 6
	stl 7
	ldl 7
	ldl 6
	ajw +8
	ret
