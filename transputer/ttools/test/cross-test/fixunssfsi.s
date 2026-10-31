.text
	.align 4;
LC@0:
	.word 0x4f000000	// float 2147483648
	.align 4;
LC@1:
	.word 0xcf000000	// float -2147483648
.globl ___fixunssfsi;
___fixunssfsi:
	ajw -2
	ldlp 3
	fpldnlsn
	ldlp 3
	fpstnlsn
	ldc LC@0-LF8
	ldpi
LF8:
	fpldnlsn
	ldlp 3
	fpldnlsn
	fpgt
	eqc 0
	cj L@1
	ldc LC@1-LF14
	ldpi
LF14:
	ldlp 3
	fpldnlsn
	fpldnladdsn
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
	fpldnlsn
	fpurz
	fpint
	ldlp 0
	fpstnli32
	ldl 0
	ajw +2
	ret
