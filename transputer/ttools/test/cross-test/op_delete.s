.globl _free;
.text
.globl ___builtin_delete;
___builtin_delete:
	ldl 1
	cj L@1
	ldl 1
	call _free
L@1:
	ret
