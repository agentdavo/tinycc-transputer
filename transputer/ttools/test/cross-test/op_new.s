.globl _malloc;
.globl ___new_handler;
.text
.globl ___builtin_new;
___builtin_new:
	ajw -1
	ldl 2
	eqc 0
	cj L@1
	ldc 1
	stl 2
L@1:
	ldl 2
	call _malloc
	stl 0
L@2:
	ldl 0
	cj L@4
	j L@3
L@4:
	ldc ___new_handler-LF24
	ldpi
LF24:
	ldnl 0
	ajw -1
	ldc 2
	ldpi
	stl 0
	gcall
	ajw -3
	ldl 2
	call _malloc
	stl 0
	j L@2
L@3:
	ldl 0
	ajw +1
	ret
