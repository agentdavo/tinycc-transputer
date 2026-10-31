.globl ___default_new_handler;
.globl ___new_handler;
.data
	.align 4;
___new_handler:
	.word ___default_new_handler
.text
.globl _set_new_handler;
_set_new_handler:
	ldc ___new_handler-LF6
	ldpi
LF6:
	ldnl 0
	ldl 1
	eqc 0
	cj L@2
	ldc ___default_new_handler-LF10
	ldpi
LF10:
	stl 1
	ldc 0
L@2:
	rev
L@1:
	ldl 1
	ldc ___new_handler-LF17
	ldpi
LF17:
	stnl 0
	ret
.globl _write;
LC@0:
	.ascii "Virtual memory exceeded in `new'\12\0"
.globl __exit;
.globl ___default_new_handler;
___default_new_handler:
	ldc 34
	ldc LC@0-LF29
	ldpi
LF29:
	ldc 2
	call _write
	ldc -1
	call __exit
