/* ---------------------------------------------- */
/* allocaTransputer.S */

PAGE 60,132

	.TRANSPUTER
	MemStart    EQU 0x80000070
	ResetCode   EQU 0x7FFFFFFE

	ORG         MemStart - 1
	DB		  STOP - START
	ORG		  MemStart

j macro val
	db 000h or (val and 00fh)
	endm

ldl macro val
 	db 070h or (val and 00fh)
	endm

rev macro
 	db 0F0h
	endm

adc macro
	db 080h or (val and 00fh)
	endm

ldc macro val
	db 040h or (val and 00fh)
	endm 

and macro
	db 24h, f6h
	endm

cj  macro val
	db 0A0h or (val and 00fh)
	endm

gt macro 
	db 0F9h
	endm

sub macro
	db 0FCh
	endm

stl macro
	db 0D0h or (val and 00fh)
	endm

ret macro
	db 022h, 0F0h
	endm

START:
	j	alloca ???

alloca:
	ldl 	  0
	ldl	  4
	
	rev
	adc	  15
	ldc	  -16
     and	  
	cj	  p3
p1:
	ldc	  4096
	gt
	cj	  p2
p2:
	ldl	  0
	sub
p3:
	stl	  0
	ret

STOP:
	END

/* ---------------------------------------------- */
