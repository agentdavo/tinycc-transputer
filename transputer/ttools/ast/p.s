.globl _printf
.globl xixixi
.globl aaa
.text
aaa:
	.need xixixi
	.byte 1,"xi\7xi"
	.align 4
	.word 0x555
	ldc 3221
	call _printf
	j aaa
	
