.globl ___builtin_new;
.text
.globl ___builtin_vec_new;
___builtin_vec_new:
	ldl 1
	call ___builtin_new
	ret
