/* $Id: cmd.c,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#include "shared.h"

/* Tell how many bytes would occupy a direct command with the argument
   `arg' -- compute the prefix sequence length */

int
cmd_size (target_word_t arg)
{
    int sz = 1;

    if (arg < 0) {
	if (arg >= -16)	/* fffffff0 */
	    sz++;	/* operands in [0xfffffff0..0xffffffff]
			   require an extra nfix only to obtain
			   sign-extension */
	arg = ~arg;
    }
    while ((arg = (target_word_unsigned_t) arg >> 4))
	sz++;

    return sz;
}


/* Generate code for a direct command with the given opcode and
   argument.  Unless `slot' is given, `buf' should have enough room
   for the longest command possible.

   Returns the size of the command generated, or negative if the
   commant does not fit in the slot. */

int
cmd_generate (char *buf, int opcode, target_word_t arg, int slot)
{
    char *q, *z;
    int len;

    q = z = &buf[slot? slot: CMD_MAXLEN];

    if (q <= buf)  return -1;
    *--q = opcode | (arg & 0xf);
    if (arg >= 0) {
	while ((arg = (target_word_unsigned_t) arg >> 4)) {
	    if (q <= buf)  return -1;
            *--q = OPCODE_PFIX | (arg & 0xf);
	}
    }
    else {
        arg = ~arg;
	while ((arg = (target_word_unsigned_t) arg >> 4) >= 16) {
	    if (q <= buf)  return -1;
            *--q = OPCODE_PFIX | (~arg & 0xf);
	}
	if (q <= buf)  return -1;
        *--q = OPCODE_NFIX | (arg & 0xf);
    }

    /* if slot length is specified, pad the command from left with NOPs */
    if (slot) {
	len = slot;
        while (q > buf)
            *--q = OPCODE_PFIX;
    }
    else {
	len = z-q;
	/* Shift the command to the start of buffer, where it should be */
	while (q < z)
	    *buf++ = *q++;
    }

    return len;
}
