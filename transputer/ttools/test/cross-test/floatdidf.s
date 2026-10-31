.globl ___negdi2;
.text
	.align 4;
LC@0:
	.word 0x0, 0x40f00000	// double 65536
	.align 4;
LC@1:
	.word 0x0, 0x0	// double 0
.globl ___floatdidf;
___floatdidf:
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
	ldl 3
	ldl 2
	ldc 32
	lshr
	stl 0
	ldlp 0
	fpb32tor64
	ldlp 4
	fpstnldb
	ldc LC@0-LF49
	ldpi
LF49:
	ldlp 4
	fpldnldb
	ldc LC@0-LF54
	ldpi
LF54:
	fpldnldb
	fpmul
	ldlp 4
	fpstnldb
	ldlp 4
	fpldnldb
	ldc LC@0-LF62
	ldpi
LF62:
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
	cj L@3
	fpldzerodb
	ldlp 9
	fpldnldb
	fpsub
	ldlp 9
	fpstnldb
L@3:
	ldlp 9
	fpldnldb
	ajw +11
	ret
