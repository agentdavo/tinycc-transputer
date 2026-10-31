.globl ___main;
.data
_message@@2:
	.ascii "\12%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\12 G r e e t i n g s   f r o m   r o o t   t r a n s p u t e r       \12%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\12\12(hit Ctrl-C to get back to shell prompt)\12\0"
.globl _boot_link_out;
.text
.globl _main;
_main:
	call ___main
	ldc _message@@2-LF5
	ldpi
LF5:
	ldc _boot_link_out-LF6
	ldpi
LF6:
	ldnl 0
	ldc 247
	out
	ldc 0
	ret
.globl ___main;
___main:
	ret
