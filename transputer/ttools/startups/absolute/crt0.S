/* $Id: crt0.S,v 1.1.1.1 1995/12/22 12:24:40 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

/* Simple startup for ROMable code.

   Features: no executable header, segments are placed back-to-back in
   the natural order (.head,.text,.data,.bss).  Load address is a
   constant known in link time (-Thead 0xNNNN), so lit can exploit
   that info in offset optimization.  No support for argv.

   _main is called with Wreg set to the top of on-chip memory.  */


/* Instruction sequence to load address to be loaded in Wreg */
#ifndef LOAD_WORKSPACE_ADDRESS
#define LOAD_WORKSPACE_ADDRESS()  mint; ldnlp 0x1000/4
#endif

/* Standard set of handy macros */
#define LDPI(LABEL)	ldc LABEL-9f; ldpi; 9: ;
#define LDS(LABEL)      LDPI(LABEL); ldnl 0;
#define STS(LABEL)      LDPI(LABEL); stnl 0;
#define LDSB(LABEL)     LDPI(LABEL); lb;
#define STSB(LABEL)     LDPI(LABEL); sb;
#define LEND(CTRL,LABEL) ldlp CTRL; ldc 9f-LABEL; lend; 9: ;
#define NEQC(CONST)     eqc CONST; eqc 0;
#define LE              gt; eqc 0;
#define GE              rev; LE;
#define LT              rev; gt;
#define BR              eqc 0; cj   /* traditional "branch if true" */

/* Layout of workspace used by the startup code */
#define L_LEND_PARAM  (0)
#define L_LEND_PARAM_SZ  (1)
#define L_LEND_COUNT  (L_LEND_PARAM+L_LEND_PARAM_SZ)
#define L_LEND_COUNT_SZ  (1)
#define L_LEND_CONTROL  (L_LEND_PARAM)
#define CRT_WORKSPACE_SZ  (L_LEND_COUNT+L_LEND_COUNT_SZ)

.globl _main
.globl @@hook

.globl head@@start
.globl text@@start
.globl data@@start
.globl bss@@start

.globl head@@size
.globl text@@size
.globl data@@size
.globl bss@@size

/* Define segment order and nomenclature; in partcular, this makes lit
   define bss@@size even if user code puts nothing in .bss */
.head .text .data .bss

/* Those assignments should resolve all expressions in link time, so
   executable file will contain only plain single-BLOCK segments. */

/* head@@start = should be set with ``-Thead 0xNNNN'' option given to lit */
text@@start = head@@start + head@@size
data@@start = text@@start + text@@size
bss@@start  = data@@start + data@@size

        /* reset instruction should jump here */
.head
@@hook:
	/*
	 * vital initializations
	 */

	LOAD_WORKSPACE_ADDRESS()
        gajw
	ajw -CRT_WORKSPACE_SZ

        /* Initialize heads of process' queues */
        mint; sthf
        mint; stlf

        /* Initialize channels, external event and timers */
        ldc 4+4+1+2;
        stl L_LEND_COUNT
1:      mint
        ldl L_LEND_COUNT
        mint
        wsub
        stnl -1;
        LEND (L_LEND_CONTROL, 1b)

        /* Clear error flag & halt-on-error flag */
        testerr;
        clrhalterr;

        /* Start timer (ret won't work unless this is done!) */
        ldc 0
        sttimer

        /* OK, initialization is over.  Call user code */
	call _main

	/* On return from _main, die (can you think of something better?) */
	stopp
