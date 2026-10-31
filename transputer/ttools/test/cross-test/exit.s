.globl ___do_global_dtors;
.globl __cleanup;
.globl __exit;
.text
.globl _exit;
_exit:
	call ___do_global_dtors
	call __cleanup
	ldl 1
	call __exit
