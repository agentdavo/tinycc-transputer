.text
	.align 4;
LC@0:
	.word 0x0, 0x41e00000	// double 2147483648
	.align 4;
LC@1:
	.word 0x0, 0xc1e00000	// double -2147483648
.globl ___fixunsdfsi;
___fixunsdfsi:
	ajw -2
	ldlp 3
	fpldnldb
	ldlp 3
	fpstnldb
	ldc LC@0-LF8
	ldpi
LF8:
	fpldnldb
	ldlp 3
	fpldnldb
	fpgt
	eqc 0
	cj L@1
	ldc LC@1-LF14
	ldpi
LF14:
	ldlp 3
	fpldnldb
	fpldnladddb
	fpurz
	fpint
	ldlp 0
	fpstnli32
	ldl 0
	ldnlp -536870912
	ajw +2
	ret
L@1:
	ldlp 3
	fpldnldb
	fpurz
	fpint
	ldlp 0
	fpstnli32
	ldl 0
	ajw +2
	ret
