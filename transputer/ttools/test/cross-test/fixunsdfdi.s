.globl ___cmpdi2;
.text
	.align 4;
LC@0:
	.word 0x0, 0x0	// double 0
	.align 4;
LC@1:
	.word 0x0, 0x41f00000	// double 4294967296
.globl ___fixunsdfdi;
___fixunsdfdi:
	ajw -30
	ldlp 31
	fpldnldb
	ldlp 31
	fpstnldb
	fpldzerodb
	ldlp 31
	fpldnldb
	fpgt
	cj L@1
	ldc 0
	ldc 0
	ajw +30
	ret
L@1:
	ldc LC@1-LF20
	ldpi
LF20:
	fpldnldb
	ldlp 31
	fpldnldb
	fprev
	fpdiv
	fpurz
	fpint
	ldlp 1
	stl 7
	ldl 7
	fpdup
	dup
	fpstnli32
	ldnlp 1
	fpuexpdec32
	fpstnli32
	ldl 1
	stl 5
	ldl 2
	stl 6
	ldl 5
	stl 29
	ldl 29
	stl 3
	ldc 0
	stl 4
	ldl 4
	ldl 3
	ldc 32
	lshl
	stl 3
	stl 4
	ldl 0
	stl 10
	ldc 0
	stl 0
	ldc 0
	ldl 3
	rev
	ldl 4
	dup
	rev
	call ___cmpdi2
	ldl 10
	stl 0
	ldc 1
	rev
	gt
	eqc 0
	cj L@3
	ldl 3
	stl 1
	ldl 4
	stl 2
	ldl 7
	dup
	fpb32tor64
	ldnlp 1
	fpi32tor64
	fpuexpinc32
	fpadd
	ldlp 8
	fpstnldb
	j L@2
L@3:
	ldc 1
	ldl 3
	and
	stl 11
	ldc 0
	stl 12
	ldl 4
	ldc 31
	shl
	stl 15
	ldl 3
	ldc 1
	shr
	ldl 15
	or
	stl 13
	ldl 4
	ldc 1
	shr
	stl 14
	ldl 13
	ldl 11
	or
	stl 16
	ldl 14
	stl 17
	ldl 16
	stl 1
	ldl 14
	stl 2
	ldl 7
	dup
	fpb32tor64
	ldnlp 1
	fpi32tor64
	fpuexpinc32
	fpadd
	ldlp 8
	fpstnldb
	ldlp 8
	fpldnldb
	ldlp 8
	fpldnldb
	fpadd
	ldlp 8
	fpstnldb
L@2:
	ldlp 31
	fpldnldb
	ldlp 8
	fpldnldb
	fpsub
	ldlp 31
	fpstnldb
	ldc LC@0-LF138
	ldpi
LF138:
	fpldnldb
	ldlp 31
	fpldnldb
	fpgt
	cj L@4
	ldc LC@0-LF145
	ldpi
LF145:
	fpldnldb
	ldlp 31
	fpldnldb
	fpsub
	fpurz
	fpint
	ldlp 1
	fpdup
	dup
	fpstnli32
	ldnlp 1
	fpuexpdec32
	fpstnli32
	ldl 1
	stl 18
	ldl 2
	stl 19
	ldc 0
	ldl 3
	ldl 18
	diff
	stl 20
	ldc 0
	ldl 3
	ldl 20
	ldiff
	rev
	ldl 4
	stl 21
	ldl 21
	rev
	diff
	stl 21
	ldl 20
	stl 20
	ldl 21
	stl 21
	ldl 20
	stl 3
	ldl 21
	stl 4
	j L@5
L@4:
	ldlp 31
	fpldnldb
	fpurz
	fpint
	ldlp 1
	fpdup
	dup
	fpstnli32
	ldnlp 1
	fpuexpdec32
	fpstnli32
	ldl 1
	stl 22
	ldl 2
	stl 23
	ldl 22
	stl 24
	ldc 0
	stl 25
	ldl 24
	ldl 3
	add
	stl 26
	ldc 0
	ldl 26
	ldl 3
	ldiff
	rev
	ldl 4
	stl 27
	ldl 27
	add
	stl 27
	ldl 26
	stl 26
	ldl 27
	stl 27
	ldl 26
	stl 3
	ldl 27
	stl 4
L@5:
	ldl 4
	ldl 3
	ajw +30
	ret
