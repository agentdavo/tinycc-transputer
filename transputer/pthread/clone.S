.section .text,"ax"
.global __tp_pthread_clone
__tp_pthread_clone:
	ldl 1
	ldc 220
	.byte 0x60, 0xf8	/* syscall */
	stl 3
	ldl 3
	cj .Lthread
	ldl 3
	ret
.Lthread:
	call __tp_pthread_start
	stopp
