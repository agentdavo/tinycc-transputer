.globl ___cmpdi2;
.text
	.align 4;
LC@0:
	.word 0x0, 0x0	// double 0
	.align 4;
LC@1:
	.word 0x0, 0x41f00000	// double 4294967296
.globl ___fixunssfdi;
___fixunssfdi:
	ajw -32
	ldlp 33
	fpldnlsn
	fpur32tor64
	ldlp 3
	fpstnldb
	fpldzerodb
	ldlp 3
	fpldnldb
	fpgt
	cj L@1
	ldc 0
	ldc 0
	ajw +32
	ret
L@1:
	ldc LC@1-LF22
	ldpi
LF22:
	fpldnldb
	ldlp 3
	fpldnldb
	fprev
	fpdiv
	fpurz
	fpint
	ldlp 1
	stl 9
	ldl 9
	fpdup
	dup
	fpstnli32
	ldnlp 1
	fpuexpdec32
	fpstnli32
	ldl 1
	stl 7
	ldl 2
	stl 8
	ldl 7
	stl 31
	ldl 31
	stl 5
	ldc 0
	stl 6
	ldl 6
	ldl 5
	ldc 32
	lshl
	stl 5
	stl 6
	ldl 0
	stl 12
	ldc 0
	stl 0
	ldc 0
	ldl 5
	rev
	ldl 6
	dup
	rev
	call ___cmpdi2
	ldl 12
	stl 0
	ldc 1
	rev
	gt
	eqc 0
	cj L@3
	ldl 5
	stl 1
	ldl 6
	stl 2
	ldl 9
	dup
	fpb32tor64
	ldnlp 1
	fpi32tor64
	fpuexpinc32
	fpadd
	ldlp 10
	fpstnldb
	j L@2
L@3:
	ldc 1
	ldl 5
	and
	stl 13
	ldc 0
	stl 14
	ldl 6
	ldc 31
	shl
	stl 17
	ldl 5
	ldc 1
	shr
	ldl 17
	or
	stl 15
	ldl 6
	ldc 1
	shr
	stl 16
	ldl 15
	ldl 13
	or
	stl 18
	ldl 16
	stl 19
	ldl 18
	stl 1
	ldl 16
	stl 2
	ldl 9
	dup
	fpb32tor64
	ldnlp 1
	fpi32tor64
	fpuexpinc32
	fpadd
	ldlp 10
	fpstnldb
	ldlp 10
	fpldnldb
	ldlp 10
	fpldnldb
	fpadd
	ldlp 10
	fpstnldb
L@2:
	ldlp 3
	fpldnldb
	ldlp 10
	fpldnldb
	fpsub
	ldlp 3
	fpstnldb
	ldc LC@0-LF140
	ldpi
LF140:
	fpldnldb
	ldlp 3
	fpldnldb
	fpgt
	cj L@4
	ldc LC@0-LF147
	ldpi
LF147:
	fpldnldb
	ldlp 3
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
	stl 20
	ldl 2
	stl 21
	ldc 0
	ldl 5
	ldl 20
	diff
	stl 22
	ldc 0
	ldl 5
	ldl 22
	ldiff
	rev
	ldl 6
	stl 23
	ldl 23
	rev
	diff
	stl 23
	ldl 22
	stl 22
	ldl 23
	stl 23
	ldl 22
	stl 5
	ldl 23
	stl 6
	j L@5
L@4:
	ldlp 3
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
	stl 24
	ldl 2
	stl 25
	ldl 24
	stl 26
	ldc 0
	stl 27
	ldl 26
	ldl 5
	add
	stl 28
	ldc 0
	ldl 28
	ldl 5
	ldiff
	rev
	ldl 6
	stl 29
	ldl 29
	add
	stl 29
	ldl 28
	stl 28
	ldl 29
	stl 29
	ldl 28
	stl 5
	ldl 29
	stl 6
L@5:
	ldl 6
	ldl 5
	ajw +32
	ret
