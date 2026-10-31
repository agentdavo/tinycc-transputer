# 1 "crt0.S"
 

 




 































 
// 
//#define LOAD_LOAD_ADDRESS()  mint; adc 0x1000
 


 



 



 













 






 
 









 








.globl head@@start
.globl head2@@start
.globl text@@start
.globl data@@start
.globl bss@@start

.globl head@@size
.globl head2@@size
.globl text@@size
.globl data@@size
.globl bss@@size

.globl head@@blocksize
.globl head2@@blocksize

 
.globl _lit_load_segment
.globl _lit_load_segsize
.globl _main

 
.globl @@hook
.globl _boot_link_out
.globl _boot_memory_start
.globl _boot_old_iptr
.globl _boot_old_wreg
.globl _boot_link_in

 

.head .head2 .text .data .bss

 




 

 
head2@@start = head@@start + head@@size
data@@start = text@@start + text@@size
bss@@start  = data@@start + data@@size

 
head@@blocksize = 10*1024
head2@@blocksize = 10*1024


 



  





.head
@@hook:
	.need second_phase

         







        call 1f
1:
	 


	ldc  workspace -9f; ldpi; 9: ; 
	gajw

	 

        ldl ((_boot_memory_start-workspace)/4) 
        adc -(1b-@@hook)
        ldl ((_boot_memory_start-workspace)/4) 

         
        ldl ((_boot_link_in-workspace)/4) 
        ldnlp -4
        stl ((_boot_link_out-workspace)/4) 

	 



         
        mint; sthf
        mint; stlf

         
        ldc 4+4+1+2;
        stl ((0) +(1) ) 
1:      mint
        ldl ((0) +(1) ) 
        mint
        wsub
        stnl -1;
        ldlp  ((0) )  ; ldc 9f-  1b ; lend; 9: ; 

         
        testerr;
        clrhalterr;

         
        ldc 0
        sttimer


	 



	 

	ldc  first_phase_top -9f; ldpi; 9: ; 
	ldl ((_boot_link_in-workspace)/4) 
        ldc 9 
        in

	 
	ldc 0
	stl (((0) +(1) ) +(1) ) 
	ldlp (((0) +(1) ) +(1) ) 
	ldl ((_boot_link_in-workspace)/4) 
        ldc 1
        in

	 

	ldl (((0) +(1) ) +(1) ) 
	ldc 252	 
	diff
	cj 1f
	stopp
1:
	 
	ldc 0
	stl (((0) +(1) ) +(1) ) 
	ldlp (((0) +(1) ) +(1) ) 
	ldl ((_boot_link_in-workspace)/4) 
        ldc 2
        in

	 
	ldc  first_phase_top -9f; ldpi; 9: ; 
	ldl ((_boot_link_in-workspace)/4) 
	ldl (((0) +(1) ) +(1) ) 	 
        in






	 

	j first_phase_top


	 


	.align 4
safetyspace:
	.word ?[5]

	 


workspace:
	.word ?[((((0) +(1) ) +(1) ) +(1) ) ]

	 


_boot_link_out::
	.word ?
_boot_memory_start::
	.word ?
_boot_old_iptr::
	.word ?
_boot_old_wreg::
	.word ?
_boot_link_in::
	.word ?

	 
	.align 4
first_phase_top:


 



.head2
second_phase:
	.need main_trampoline











	 

	ldc (0x80000000 + 256*1024)  - ((((0) +(4) ) +(1) ) +(3) ) *4;
	gajw

	 




	ldc  head@@start -9f; ldpi; 9: ; 	
	stl (((0) +(4) ) +(1) ) 
	ldc  head2@@start -9f; ldpi; 9: ; 
	stl (((0) +(4) ) +(1) ) +1
	ldc  head2@@start+head2@@size -9f; ldpi; 9: ;  
	stl (((0) +(4) ) +(1) ) +2

	 



	 
	ldl (((0) +(4) ) +(1) ) +2
	stl ((0) +(4) ) 
1:





	 
	ldc 3; stl 0

	 
	ldlp (((0) +(4) ) +(1) ) 

	 
	ldc (0x80000000 + 256*1024) -(200*4)	 
	ldl ((0) +(4) ) 
	diff

	 
	ldl ((0) +(4) ) 

	call _lit_load_segment		 
	cj 1f				 

	 
	ldc   _lit_load_segsize  -9f; ldpi; 9: ; ; ldnl 0; 
	ldl ((0) +(4) ) 
	add
	stl ((0) +(4) ) 

	 
	j 1b

1:
	 








	ldl (((0) +(4) ) +(1) ) +2
	gcall

	 
	.align 4
second_phase_top:


 






.text
main_trampoline:
	call _main
	stopp


 
