.globl ___DTOR_LIST__;
.text
.globl ___do_global_dtors;
___do_global_dtors:
	ajw -2
	ldc ___DTOR_LIST__+4-LF4
	ldpi
LF4:
	stl 0
L@1:
	ldl 0
	ldnl 0
	stl 1
	ldl 1
	eqc 0
	cj L@4
	ajw +2
	ret
L@4:
	ldl 0
	ldnlp 1
	stl 0
	ldl 1
	ajw -1
	ldc 2
	ldpi
	stl 0
	gcall
	ajw -3
	j L@1
.globl ___CTOR_LIST__;
.globl ___do_global_ctors;
___do_global_ctors:
	ajw -3
	ldc ___CTOR_LIST__-LF32
	ldpi
LF32:
	ldnl 0
	stl 0
	ldl 0
	eqc -1
	cj L@8
	ldc 0
	stl 0
	ldc ___CTOR_LIST__+4-LF40
	ldpi
LF40:
	stl 2
L@9:
	ldl 2
	ldnl 0
	eqc 0
	cj L@11
	j L@8
L@11:
	ldl 2
	ldnlp 1
	stl 2
	ldl 0
	adc 1
	stl 0
	j L@9
L@8:
	ldl 0
	stl 1
L@13:
	ldl 1
	eqc 0
	cj L@16
	ajw +3
	ret
L@16:
	ldl 1
	ldc ___CTOR_LIST__-LF73
	ldpi
LF73:
	wsub
	ldnl 0
	ajw -1
	ldc 2
	ldpi
	stl 0
	gcall
	ajw -3
	ldl 1
	adc -1
	stl 1
	j L@13
.data
	.align 4;
_initialized@@6:
	.word 0
.text
.globl ___main;
___main:
	ldc _initialized@@6-LF93
	ldpi
LF93:
	ldnl 0
	eqc 0
	cj L@18
	ldc 1
	ldc _initialized@@6-LF98
	ldpi
LF98:
	stnl 0
	call ___do_global_ctors
L@18:
	ret
