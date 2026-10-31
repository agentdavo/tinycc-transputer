.globl ___builtin_delete;
.text
.globl ___builtin_vec_delete;
___builtin_vec_delete:
	ldl 1
	call ___builtin_delete
	ret
