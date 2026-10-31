/* $Id: litin.c,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#include "shared.h"
#include <errno.h>


/* File to input from. */

FILE *lit_in_file = NULL;
char *lit_in_file_name = "?";

/* lit_in_string leaves the legth of the string here */

int lit_in_string_len;	


/* Input encoded NUMBER from the `lit_in_file'.  Return the number read. */

long
lit_in_number ()
{
    int invert = 0;
    long number = 0;
    int c = getc (lit_in_file);
    
    if (c == 255) {
	invert++;
	c = getc (lit_in_file);
    }

    switch (c) {
    case 255:
	error2 (lit_in_file_name, ftell (lit_in_file) - 1,
		"unexpected byte 0x%02x in encoded number\n", c);
	return 0;
    case 254:
	error2 (lit_in_file_name, ftell (lit_in_file) - 1,
		"eight-byte encoded numbers not supported\n");
	return 0;
    case 253:
	number |= (c = getc (lit_in_file));
	number |= (c = getc (lit_in_file)) << 8;
	number |= (c = getc (lit_in_file)) << 16;
	number |= (c = getc (lit_in_file)) << 24;
	break;
    case 252:
	number |= (c = getc (lit_in_file));
	number |= (c = getc (lit_in_file)) << 8;
	break;
    case 251:
	number |= (c = getc (lit_in_file));
	break;
    default:
	number = c;
    }

    if (c == EOF)
	fatal ("%s: %s", lit_in_file_name, strerror (errno));

    if (invert)
	number = ~number;

    return number;
}


/* Input a LIT format BLOCK from `lit_in_file'. */

char *
lit_in_block (size_t len)
{
    char *blk = (char *) xmalloc (len);

    if (fread (blk, 1, len, lit_in_file) != len)
	fatal ("%s: %s", lit_in_file_name, strerror (errno));

    return blk;
}

/* Input a LIT format STRING from `lit_in_file'. */

char *
lit_in_string ()
{
    int len = lit_in_number ();
    char *str = (char *) xmalloc (len+1);

    if (fread (str, 1, len, lit_in_file) != len)
	fatal ("%s: %s", lit_in_file_name, strerror (errno));

    str[len] = '\0';

    lit_in_string_len = len;
    return str;
}

expr
lit_in_expr ()
{
    static expr *estack = NULL;
    static int estack_allocated = 0;
    int ecount = 0;		/* index of the first free estack entry */
    long tag;
    expr e;

    while ((tag = lit_in_number ()) != LIT_EXPR_END) {

	if (tag < 0 || tag >= EXPR_TAG_COUNT)
	    fatal ("%s: %u: invalid expression tag %d\n",
		   lit_in_file_name, ftell (lit_in_file), tag);

	/* Now we are likely to push something onto estack; let's grow
           it beforehand to have at least one free entry */

	if (estack_allocated <= ecount) {
	    estack_allocated = estack_allocated? estack_allocated * 2: 32;
	    estack = (expr *) xrealloc (estack,
					estack_allocated * sizeof (expr));
	}

	if (EXPR_TAG_ISLEAF (tag)) {
	    switch ((int) tag) {
	    case EXPR_CONST:
		estack[ecount++] = expr_const (lit_in_number ());
		break;
	    case EXPR_NAME:
		estack[ecount++] =
		    expr_name (name_by_index (lit_in_number ()));
		break;
	    case EXPR_ELEMENT:
		estack[ecount++] = element_by_index (lit_in_number ());
		break;
	    case EXPR_ELSUM:
		estack[ecount++] = expr_elsum ();
		while ((tag = lit_in_number ()) != -1) {
		    e = element_by_index (tag);
		    expr_elsum_append (estack[ecount-1], e);
		    EXPR_FREE (e);
		}
		break;
	    case EXPR_RANGE:
		{
		    long n = lit_in_number ();
		    estack[ecount++] = expr_range (n, lit_in_number ());
		}
		break;
	    }
	}
	else if (EXPR_TAG_ISUNARY (tag)) {
	    if (ecount < 1)
		fatal ("%s: %u: no operand for unary operator\n",
		       lit_in_file_name, ftell (lit_in_file));
	    e = expr_unop ((int) tag, estack[ecount-1]);
	    EXPR_FREE (estack[ecount-1]);
	    estack[ecount-1] = e;
	}
	else { /* binary */
	    if (ecount < 2)
		fatal ("%s: %u: no operands for binary operator\n",
		       lit_in_file_name, ftell (lit_in_file));
	    e = expr_binop ((int) tag, estack[ecount-2], estack[ecount-1]);
	    EXPR_FREE (estack[ecount-1]);
	    EXPR_FREE (estack[ecount-2]);
	    estack[ecount-2] = e;
	    ecount--;
	}
    }

    /* "end of expression sign" reached */
    if (ecount != 1)
	fatal ("%s: %u: premature LIT_EXPR_END\n",
	       lit_in_file_name, ftell (lit_in_file));

    return estack[0];
}


char *
lit_area_name (long area)
{
    switch (area) {
    case LIT_AREA_NAMETABLE:	return "NAMETABLE";
    case LIT_AREA_NEEDS:	return "NEEDS";
    case LIT_AREA_DEFINITIONS:	return "DEFINITIONS";
    case LIT_AREA_SEGMENTS:	return "SEGMENTS";
    case LIT_AREA_STABS:	return "STABS";
    default:
	{
	    static char buf[sizeof ("illegal area code 0x12345678")];
	    sprintf (buf, "illegal area code 0x%lx", area);
	    return buf;
	}
    }
}
