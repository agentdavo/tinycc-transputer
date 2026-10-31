.globl _abort;
.text
.globl ___builtin_saveregs;
___builtin_saveregs:
	call _abort
