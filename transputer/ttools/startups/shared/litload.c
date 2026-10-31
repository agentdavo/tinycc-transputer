/* $Id: litload.c,v 1.1.1.1 1995/12/22 12:24:39 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

/* Generic LIT format loader routine.

   This module uses two functions to read the file being loaded:
   lit_load_in_byte (void) and lit_load_in_block (buf, count) that
   you must #define or link in.  And lit_load_fatal () as well.  */

#include "litload.h"


/* Own versions of limits, as this file should not depend on libc */
#undef INT_MIN
#undef INT_MAX
#define INT_MIN ((signed int) (~0 & ~(~0U>>1)))
#define INT_MAX ((signed int)(~0U>>1))


/* Set by lit_load_segment: size of the last segment loaded */
long lit_load_segsize = 0;

/* Set by lit_load_segment: name of the next segment or "\0" if final
   MARKER reached */
char lit_load_segname[MAX_SEGMENT_NAME_LENGTH+1];

/* Count of how many bytes the last number input with
   lit_load_in_number occupied in the lit file.  Useful for figuring
   out the length of final MARKER padding.  */
static int last_number_len;


static long lit_load_in_expr (long *ntab, int ntabsz);
static long lit_load_in_number (void);
static int lit_load_cmd_generate (char *buf, int opcode, long arg, int slot);


/* Load one segment from the file to the buffer pointed to by `area'.
   `ntab' is a table of segment addresses which can be referred to
   from NAME expressions.  `ntabsz' may be lower than the actual
   number of segments in the file being loaded; this is ok provided
   there are no NAME expression referring to the absent segments
   addresses; in particular, if ntabsz is zero, no EXPR_NAME is legal.
   This is useful for (eg) loading the .head segment containing
   segment table, containing the info necessary to build `ntab'.

   Return 1 if there are more segments to load, 0 if not
   (end-of-segment-area marker has been reached).  lit_load_segsize is
   set to the size of the segment just loaded, lit_load_segname is
   filled with the name of the next segment, if any. */

int
lit_load_segment (char *area, int areasz, long *ntab, int ntabsz)
{
    long ltag;
    int i;

    lit_load_segsize = 0;

    for (;;) {

	lit_load_debug (__LINE__|0x77770000);

	ltag = lit_load_in_number ();
	if (ltag < INT_MIN || ltag > INT_MAX)
	    lit_load_fatal (__LINE__, "record tag 0x%lx outside bounds\n", ltag);

	lit_load_debug (__LINE__|0x77770000);

	if (ltag > 0) {  /* BLOCK */
	    lit_load_debug (__LINE__|0x77770000);
	    if (ltag > areasz)
		lit_load_fatal (__LINE__, "area overflowed\n", 0);
	    lit_load_in_block (area, ltag);
	    area += ltag;
	    areasz -= ltag;
	    lit_load_segsize += ltag;
	    continue;
	}

	switch ((int) ltag) {

	case LIT_DUMMY:
	    lit_load_debug (__LINE__|0x77770000);
	    break;

	case LIT_DATA:
	    lit_load_debug (__LINE__|0x77770000);
	    {
		int width = lit_load_in_number ();
		long value = lit_load_in_expr (ntab, ntabsz);
		long count = lit_load_in_expr (ntab, ntabsz);
		long size = lit_load_in_expr (ntab, ntabsz);

		if (count < 0)
		    lit_load_fatal (__LINE__, "DATA element has negative count\n", 0);

		/* `size' may be smaller than count*width if count was
		   not known at pass_freeze; we will fill up to
		   `size' in this case.  But `size' can never be
		   smaller than count*width.  */

		if (count*width > size)
		    lit_load_fatal (__LINE__, "inconsistent DATA element\n", 0);
		if (size > areasz)
		    lit_load_fatal (__LINE__, "area overflowed\n", 0);

#ifdef TARGET_LITTLE_ENDIAN
		switch (width) {
		case 4:
		    for (i=0; i < count; i++) {
			*area++ = value;
			*area++ = value >> 8;
			*area++ = value >> 16;
			*area++ = value >> 24;
		    }
		    break;
		case 2:
		    for (i=0; i < count; i++) {
			*area++ = value;
			*area++ = value >> 8;
		    }
		    break;
		case 1:
		    for (i=0; i < count; i++) {
			*area++ = value;
		    }
		    break;
		default:
		    lit_load_fatal (__LINE__, "illegal data width %ld\n", width);
		}
#else
		lit_load_fatal (__LINE__, "big-endian targets not supported", 0);
#endif
		/* fill up to size */
		i = size - width*count;
		while (i--)
		    *area++ = value;

		areasz -= size;
		lit_load_segsize += size;
	    }
	    break;

	case LIT_CMD:
	    lit_load_debug (__LINE__|0x77770000);
	    {
		int opcode = lit_load_in_number ();
		long arg = lit_load_in_expr (ntab, ntabsz);
		long size = lit_load_in_expr (ntab, ntabsz);

		lit_load_debug (__LINE__|0x77770000);
		lit_load_debug (opcode);
		lit_load_debug (arg);
		lit_load_debug (size);

		if (size > areasz)
		    lit_load_fatal (__LINE__, "area overflowed\n", 0);
		    
		lit_load_cmd_generate (area, opcode, arg, size);

		area += size;
		areasz -= size;
		lit_load_segsize += size;
	    }
	    break;

	case LIT_MARKER:
	    lit_load_debug (__LINE__|0x77770000);
	    {
		int len = last_number_len;  /* length of the MARKER record tag */
		int area_code;
		int area_length;

		area_code = lit_load_in_number ();
		len += last_number_len;
		area_length = lit_load_in_number ();
		len += last_number_len;
		
		/* shut up "unused variable" warnings */
		(void) area_code;
		(void) area_length;

		/* Read out the marker padding, be a good boy */
		while (len < LIT_MARKER_LENGTH) {
		    lit_load_in_byte ();
		    len++;
		}

		lit_load_segname[0] = '\0';
		return 0;
	    }

	case LIT_SEGMENT:
	    lit_load_debug (__LINE__|0x77770000);
	    {
		int segname_len = lit_load_in_number ();

		if (segname_len > MAX_SEGMENT_NAME_LENGTH)
		    lit_load_fatal (__LINE__, "segment name too long: %d characters\n",
			   segname_len);
		lit_load_in_block (lit_load_segname, segname_len);
		lit_load_in_number ();  /* gobble up default fragment index */
		return 1;
	    }

	default:
	    lit_load_fatal (__LINE__, "unexpected record tag 0x%lx\n", ltag);
	}
    }
    /*NOTREACHED*/
}


static int
lit_load_cmd_generate (char *buf, int opcode, long arg, int slot)
{
    char *q, *z;

    q = z = &buf[slot];

    if (q <= buf)  lit_load_fatal (__LINE__, "area overflowed\n", 0);
    *--q = opcode | (arg & 0xf);
    if (arg >= 0) {
	while ((arg = (target_word_unsigned_t) arg >> 4)) {
	    if (q <= buf)  lit_load_fatal (__LINE__, "area overflowed\n", 0);
            *--q = OPCODE_PFIX | (arg & 0xf);
	}
    }
    else {
        arg = ~arg;
	while ((arg = (target_word_unsigned_t) arg >> 4) >= 16) {
	    if (q <= buf)  lit_load_fatal (__LINE__, "area overflowed\n", 0);
            *--q = OPCODE_PFIX | (~arg & 0xf);
	}
	if (q <= buf)  lit_load_fatal (__LINE__, "area overflowed\n", 0);
        *--q = OPCODE_NFIX | (arg & 0xf);
    }

    /* pad the command from left with NOPs */
    while (q > buf)
	*--q = OPCODE_PFIX;

    return slot;
}


static long
lit_load_in_number ()
{
    int invert = 0;
    long number = 0;
    int c = lit_load_in_byte ();

    last_number_len = 1;

    if (c == 255) {
	invert++;
	c = lit_load_in_byte ();
	last_number_len++;
    }

    switch (c) {
    case 255:
	lit_load_fatal (__LINE__, "unexpected byte 0x%02x in encoded number\n", c);
	return 0;
    case 254:
	lit_load_fatal (__LINE__, "eight-byte encoded numbers not supported\n", 0);
	return 0;
    case 253:
	number |= (lit_load_in_byte ());
	number |= (lit_load_in_byte ()) << 8;
	number |= (lit_load_in_byte ()) << 16;
	number |= (lit_load_in_byte ()) << 24;
	last_number_len += 4;
	break;
    case 252:
	number |= (lit_load_in_byte ());
	number |= (lit_load_in_byte ()) << 8;
	last_number_len += 2;
	break;
    case 251:
	number |= (lit_load_in_byte ());
	last_number_len++;
	break;
    default:
	number = c;
    }

    if (invert)
	number = ~number;

    return number;
}

static long
lit_load_in_expr (long *ntab, int ntabsz)
{
    long tag;
    long n;
    int estack[MAX_EXPRESSION_DEPTH];
    int ecount = 0;		/* index of the first free estack entry */

    while ((tag = lit_load_in_number ()) != LIT_EXPR_END) {

	lit_load_debug (__LINE__|0x77770000);
	lit_load_debug (tag);

	if (tag < 0 || tag >= EXPR_TAG_COUNT)
	    lit_load_fatal (__LINE__, "invalid expression tag %ld\n", tag);

	if (EXPR_TAG_ISLEAF (tag)) {
	    if (ecount >= MAX_EXPRESSION_DEPTH)
		lit_load_fatal (__LINE__, "MAX_EXPRESSION_DEPTH exceeded\n", 0);
	    
	    switch ((int) tag) {
	    case EXPR_CONST:
		estack[ecount++] = lit_load_in_number ();
		break;
	    case EXPR_NAME:
		n = lit_load_in_number ();
		if (n >= ntabsz)
		    lit_load_fatal (__LINE__, "segment %ld start address unknown\n", n);
		estack[ecount++] = ntab[n];
		break;
	    case EXPR_ELEMENT:
		lit_load_fatal (__LINE__, "illegal %s expression in loadable file\n",
		       "ELEMENT");
		break;
	    case EXPR_RANGE:
		lit_load_fatal (__LINE__, "illegal %s expression in loadable file\n",
		       "RANGE");
		break;
	    }
	}

	else if (EXPR_TAG_ISUNARY (tag)) {
	    if (ecount < 1)
		lit_load_fatal (__LINE__, "expression garbled\n", 0);

	    switch ((int) tag) {
	    case EXPR_NEG:
		estack[ecount-1] = - estack[ecount-1];
		break;
	    case EXPR_NOT:
		estack[ecount-1] = ! estack[ecount-1];
		break;
	    case EXPR_COMPL:
		estack[ecount-1] = ~ estack[ecount-1];
		break;
	    default:
		lit_load_fatal (__LINE__, "internal error: unknown unary tag %ld\n", tag);
	    }
	}

	/* conditional expressions appear as (cond iftrue iffalse ? :) */
	else if (tag == EXPR_QUEST) {
	    if ((tag = lit_load_in_number ()) != EXPR_COLON)
		lit_load_fatal (__LINE__, "expression garbled\n", 0);
	    if (ecount < 3)
		lit_load_fatal (__LINE__, "expression garbled\n", 0);
	    estack[ecount-3] = (estack[ecount-3] ? estack[ecount-2]
				: estack[ecount-1]);
	    ecount -= 2;
	}

	else /* binary */ {
	    if (ecount < 2)
		lit_load_fatal (__LINE__, "expression garbled\n", 0);

	    switch ((int) tag) {
	    case EXPR_MINUS:
		estack[ecount-2] = estack[ecount-2] - estack[ecount-1];
		break;
	    case EXPR_PLUS:
		estack[ecount-2] = estack[ecount-2] + estack[ecount-1];
		break;
	    case EXPR_LSH:
		estack[ecount-2] = estack[ecount-2] << estack[ecount-1];
		break;
	    case EXPR_RSH:
		estack[ecount-2] = estack[ecount-2] >> estack[ecount-1];
		break;
	    case EXPR_MUL:
		estack[ecount-2] = estack[ecount-2] * estack[ecount-1];
		break;
	    case EXPR_DIV:
		if (estack[ecount-1] == 0)
		    lit_load_fatal (__LINE__, "divide by zero in expression\n", 0);
		estack[ecount-2] = estack[ecount-2] / estack[ecount-1];
		break;
	    case EXPR_REM:
		if (estack[ecount-1] == 0)
		    lit_load_fatal (__LINE__, "divide by zero in expression\n", 0);
		estack[ecount-2] = estack[ecount-2] % estack[ecount-1];
		break;
	    case EXPR_AND:
		estack[ecount-2] = estack[ecount-2] & estack[ecount-1];
		break;
	    case EXPR_XOR:
		estack[ecount-2] = estack[ecount-2] ^ estack[ecount-1];
		break;
	    case EXPR_OR:
		estack[ecount-2] = estack[ecount-2] | estack[ecount-1];
		break;
	    case EXPR_LT:
		estack[ecount-2] = estack[ecount-2] < estack[ecount-1];
		break;
	    case EXPR_LE:
		estack[ecount-2] = estack[ecount-2] <= estack[ecount-1];
		break;
	    case EXPR_GT:
		estack[ecount-2] = estack[ecount-2] > estack[ecount-1];
		break;
	    case EXPR_GE:
		estack[ecount-2] = estack[ecount-2] >= estack[ecount-1];
		break;
	    case EXPR_EQ:
		estack[ecount-2] = estack[ecount-2] == estack[ecount-1];
		break;
	    case EXPR_NE:
		estack[ecount-2] = estack[ecount-2] != estack[ecount-1];
		break;
	    case EXPR_ANDAND:
		estack[ecount-2] = estack[ecount-2] && estack[ecount-1];
		break;
	    case EXPR_OROR:
		estack[ecount-2] = estack[ecount-2] || estack[ecount-1];
		break;
	    default:
		lit_load_fatal (__LINE__, "internal error: unknown binary tag %ld\n", tag);
	    }
	    ecount--;
	}
    }

    lit_load_debug (__LINE__|0x77770000);
    lit_load_debug (tag);

    /* "end of expression sign" reached */
    if (ecount != 1)
	lit_load_fatal (__LINE__, "expression garbled\n", 0);

    return estack[0];
}
