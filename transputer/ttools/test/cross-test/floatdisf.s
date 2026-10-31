.globl ___negdi2;
.text
	.align 4;
LC@0:
	.word 0x0, 0x40f00000	// double 65536
	.align 4;
LC@1:
	.word 0x0, 0x0	// double 0
.globl ___floatdisf;
___floatdisf:
	ajw -11
	ldl 12
	stl 2
	ldl 13
	stl 3
	ldc 0
	stl 6
	ldl 6
	ldl 3
	gt
	eqc 0
	cj L@2
	ldl 3
	eqc 0
	cj L@1
	ldl 2
	eqc 0
	cj L@1
	j L@1
L@2:
	ldl 3
	ldl 2
	call ___negdi2
	stl 7
	stl 8
	ldl 7
	stl 2
	ldl 8
	stl 3
	ldc 1
	stl 6
L@1:
	ldc 0
	ldc 2097151
	ldl 3
	ldiff
	rev
	eqc 0
	cj L@5
	ldl 3
	eqc 2097151
	cj L@3
	ldc 0
	ldc -1
	ldl 2
	ldiff
	rev
	eqc 0
	cj L@5
	ldl 2
	eqc -1
	cj L@3
	j L@3
L@5:
	ldc 2047
	ldl 2
	and
	cj L@3
	ldc 2048
	ldl 2
	or
	ldl 3
	rev
	stl 2
	stl 3
L@3:
	ldl 3
	ldl 2
	ldc 32
	lshr
	stl 0
	ldlp 0
	fpb32tor64
	ldlp 4
	fpstnldb
	ldc LC@0-LF89
	ldpi
LF89:
	ldlp 4
	fpldnldb
	ldc LC@0-LF94
	ldpi
LF94:
	fpldnldb
	fpmul
	ldlp 4
	fpstnldb
	ldlp 4
	fpldnldb
	ldc LC@0-LF102
	ldpi
LF102:
	fpldnldb
	fpmul
	ldlp 4
	fpstnldb
	ldl 2
	stl 0
	ldlp 0
	fpb32tor64
	ldlp 4
	fpldnldb
	fpadd
	ldlp 9
	fpstnldb
	ldl 6
	cj L@7
	fpldzerodb
	ldlp 9
	fpldnldb
	fpsub
	ldlp 9
	fpstnldb
L@7:
	ldlp 9
	fpldnldb
	fpur64tor32
	ajw +11
	ret
