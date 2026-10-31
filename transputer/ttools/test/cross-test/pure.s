.globl _write;
.text
LC@0:
	.ascii "pure virtual method called\12\0"
.globl __exit;
.globl ___pure_virtual;
___pure_virtual:
	ldc 27
	ldc LC@0-LF5
	ldpi
LF5:
	ldc 2
	call _write
	ldc -1
	call __exit
