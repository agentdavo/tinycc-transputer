.globl ___fixunssfdi;
.globl ___negdi2;
.text
	.align 4;
LC@0:
	.word 0x0	// float 0
.globl ___fixsfdi;
___fixsfdi:
	ajw -8
	ldlp 9
	fpldnlsn
	ldlp 9
	fpstnlsn
	ldc LC@0-LF10
	ldpi
LF10:
	fpldnlsn
	ldlp 9
	fpldnlsn
	fpgt
	cj L@1
	ldc LC@0-LF17
	ldpi
LF17:
	fpldnlsn
	ldlp 9
	fpldnlsn
	fpsub
	ldlp 0
	fpstnlsn
	ldl 0
	call ___fixunssfdi
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
	fpldnlsn
	fpstnlsn
	ldl 0
	call ___fixunssfdi
	stl 6
	stl 7
	ldl 7
	ldl 6
	ajw +8
	ret
