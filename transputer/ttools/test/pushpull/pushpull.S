# 1 "pushpull.S"
//
// pushpull --
//   Simple sample program for data exchange via the communication board.
//
// The programs comprises two processes, one for input and another for
// output.  Each process is assigned a fixed link to work with.  There
// are two data buffers for input and two for output; this allows PC and
// transputer to act in parallel.  Each buffer has a status word
// associated with it, which provides for synchronization between PC and
// transputer.  Zero status indicates that the buffer is empty.  When one
// fills the buffer with data, it finishes with placing the length of the
// data into the status word.  Status of -1 indicates the end-of-file
// condition.
// 
// 940621, sizif

# 1 "pushpull.h" 1
 












 








# 17 "pushpull.S" 2




 













 






  
  
  
  


.globl head@@start;
 head@@start = 0x80008000;

	 



.globl @@hook
@@hook:
	 
	ldc 0; ldc  (0x80009000-0x20+0x00)  -9f; ldpi; 9: ; ; stnl +0;
	ldc 0; ldc  (0x80009000-0x20+0x04)  -9f; ldpi; 9: ; ; stnl +0;
	ldc 0; ldc  (0x80009000-0x20+0x08)  -9f; ldpi; 9: ; ; stnl +0;
	ldc 0; ldc  (0x80009000-0x20+0x0c)  -9f; ldpi; 9: ; ; stnl +0;


	 
	mint;
	ldc   (0x80000080)   -9f; ldpi; 9: ; ; stnl 0; 


 
# 140 "pushpull.S"



	 
	ldc push - 9f
	ldlp +0x50
	startp;
  9:
	 
	ldc pull - 9f
	ldlp +0xa0
	startp;
  9:
	 
	stopp


	 


push:
  1:
	ldc  (0x80009000-0x20+0x00)  -9f; ldpi; 9: ; 
	ldc  (0x80001000)  -9f; ldpi; 9: ; 
	call push1
	ldc  (0x80009000-0x20+0x04)  -9f; ldpi; 9: ; 
	ldc  ((0x80001000)  + (0x1000) *1)  -9f; ldpi; 9: ; 
	call push1
	j 1b






push1:
	ajw - 2 

	 
  1:
	ldtimer; tin;	// deschedule
	ldl (2 +1+1) 
	ldnl 0
	stl  1  ; ldl  1  ; 
	cj 1b

	 
  1:
        ldtimer; adc 1; tin;
	ldl 1 
	ldl (2 +1+1) 
	ldnl 0
	stl  1  ; ldl  1  ; 
	diff
	eqc 0; cj  1b

	 
	ldlp 1 	// buffer
	mint; ldnlp +0x20 	// channel
	ldc 4		// count
	out

	 

	ldl 1 ;
        eqc -1;
	eqc 0; cj  1f

	   
	  ldl (2 +1+0) 		// buffer
	  mint; ldnlp +0x20 	// channel
	  ldl 1 		// count
	  out
  1:	
         
	ldc 0
	ldl (2 +1+1) 
	stnl 0

	ajw + 2 
	ret





	 


pull:
  1:
	ldc  (0x80009000-0x20+0x08)  -9f; ldpi; 9: ; 
	ldc  ((0x80001000)  + (0x1000) *2)  -9f; ldpi; 9: ; 
	call pull1
	ldc  (0x80009000-0x20+0x0c)  -9f; ldpi; 9: ; 
	ldc  ((0x80001000)  + (0x1000) *3)  -9f; ldpi; 9: ; 
	call pull1
	j 1b






pull1:
	ajw - 2 

	 
  1:
	ldl (2 +1+1) 
	ldnl 0
	cj 1f
	ldtimer; tin;	// deschedule
	j 1b

  1:
	 
	ldlp 1 	// buffer
	mint; ldnlp +0x20  	// channel
	ldc 4		// count
	in

	 

	ldl 1 ;
        eqc -1;
	eqc 0; cj  1f

	   
	  ldl (2 +1+0) 		// buffer
	  mint; ldnlp +0x20  	// channel
	  ldl 1 		// count
	  in
  1:
	 
	ldl 1 
	ldl (2 +1+1) 
	stnl 0
	
	ajw + 2 
	ret



	
	
	
	


