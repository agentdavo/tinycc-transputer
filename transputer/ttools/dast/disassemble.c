/* $Id: disassemble.c,v 1.1.1.1 1995/12/22 12:24:39 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#include "shared.h"
#include "disassemble.h"
#include "dastabs.h"

static struct disassemble das;

struct disassemble *
disassemble (unsigned char *buf, int buflen)
{
    unsigned char *p = buf;
    char *q;
    long oreg = 0;

    assert (buflen);

    if (buflen > CMD_MAXLEN)
	buflen = CMD_MAXLEN;
    
    if (*p == OPCODE_PFIX)
	/* pfix 0 is meaningless in a prefix
	   sequence -- display separately */
	goto direct;

    for ( ; p < &buf[buflen]; p++) {
	if ((*p & 0xf0) == OPCODE_PFIX) {
	    oreg |= *p & 0x0f;
	    oreg <<= 4;
	}
	else if ((*p & 0xf0) == OPCODE_NFIX) {
	    oreg |= *p & 0x0f;
	    oreg = ~oreg;
	    oreg <<= 4;
	}
	else
	    break;
    }

    if (p == &buf[buflen]) {
	/* no cmd at the end of prefix sequence; perhaps prefix
           seq. too long (overflows Oreg).  Disassemble the first
           prefix byte separately. */
	p = buf;
	oreg = 0;
	goto direct;
    }

 direct:
    oreg |= *p & 0x0f;

    /* Substitute indirect command mnemonics for `opr X', in case it
       is found in tables.  Otherwise, just put it as `opr X'.  */

    if ((*p & 0xf0) == OPCODE_OPR
	&& oreg >= 0
	&& oreg < (sizeof (tab_indir) / sizeof (tab_indir[0]))) {
	das.hasarg = 0;
	das.mnem = tab_indir[oreg];
    }
    else if ((*p & 0xf0) == OPCODE_OPR
	     && oreg < 0 && oreg > - (sizeof (tab_indir_negative)
				      / sizeof (tab_indir_negative[0]))) {
	das.hasarg = 0;
	das.mnem = tab_indir_negative[-oreg];
    }
    /* If this command is ldc, and the following is fpentry (0xab),
       this is an fpu- command */
    else if ((*p & 0xf0) == OPCODE_LDC
	     && p + 2 < &buf[buflen]
	     && p[1] == (OPCODE_PFIX | ((OPCODE_FPENTRY >> 8) & 0x0f))
	     && p[2] == (OPCODE_OPR | (OPCODE_FPENTRY & 0x0f))
	     && oreg >= 0
	     && oreg < (sizeof (tab_fpu) / sizeof (tab_fpu[0]))) {
	das.hasarg = 0;
	das.mnem = tab_fpu[oreg];
    }
    else {
	das.hasarg = 1;
	das.pcrel = tab_pcrel[*p & 0xf0];
	das.arg = oreg;
	das.mnem = tab_direct[*p & 0xf0];
    }

    das.size = (p+1) - buf;
    q = das.dump;
    for (p = buf; p < &buf[das.size]; p++) {
	sprintf (q, "%02x ", (unsigned char) *p);
	q += 3;
    }
    *--q = '\0';

    return &das;
}



