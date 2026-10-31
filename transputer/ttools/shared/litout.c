/* $Id: litout.c,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

/* LIT format object file output routines */

#include "shared.h"
#include <errno.h>


/* File to output to.  Using global var instead of a parameter to every
   function looks reasonable; you will hardly ever want to write many files
   at once. */

FILE *lit_out_file = NULL;
char *lit_out_file_name = "?";


/* Output encoded NUMBER to the `lit_out_file'.  Return the number of
   bytes written.

   Number encoding scheme is identical to the one used in TCOFF.  We
   don't support INT64, though.

   This code should work on both little- and big-endian hosts. */

int
lit_out_number (long number)
{
    int ret = 0;

    if (number < 0 && number >= ~0xffff) {
	if (putc (255, lit_out_file) == EOF)
	    fatal ("%s: %s", lit_out_file_name, strerror (errno));
	number = ~number;
	ret++;
    }

    if (number >= 0 && number <= 250) {
	if (putc (number, lit_out_file) == EOF)
	    fatal ("%s: %s", lit_out_file_name, strerror (errno));
	ret++;
    }
    else if ((number & ~0xff) == 0) {
	if (putc (251, lit_out_file) == EOF
	    || putc (number, lit_out_file) == EOF)
	    fatal ("%s: %s", lit_out_file_name, strerror (errno));
	ret += 2;
    }
    else if ((number & ~0xffff) == 0) {
	if (putc (252, lit_out_file) == EOF
	    || putc (number, lit_out_file) == EOF
	    || (number >>=8, putc (number, lit_out_file)) == EOF)
	    fatal ("%s: %s", lit_out_file_name, strerror (errno));
	ret += 3;
    }
    else {
	if (putc (253, lit_out_file) == EOF
	    || putc (number, lit_out_file) == EOF
	    || (number >>=8, putc (number, lit_out_file)) == EOF
	    || (number >>=8, putc (number, lit_out_file)) == EOF
	    || (number >>=8, putc (number, lit_out_file)) == EOF)
	    fatal ("%s: %s", lit_out_file_name, strerror (errno));
	ret += 5;
    }

    return ret;
}


/* Output a LIT format STRING to the `lit_out_file'.  Return the number of
   bytes written.  */

int
lit_out_string (char *str, size_t len)
{
    int n;

    n = lit_out_number (len);

    if (fwrite (str, 1, len, lit_out_file) != len)
	fatal ("%s: %s", lit_out_file_name, strerror (errno));

    return n + len;
}

/* Output nametable entry for name `np'. */

int
lit_out_ntab_entry (name np)
{
    static int last_index = -1;
    int flags = 0;
    int ret = 0;

    assert (np->index == 0 || np->index == last_index + 1);
    last_index = np->index;

    if (np->defined)
	flags |= LIT_NAME_DEFINED;
    if (np->fragment)
	flags |= LIT_NAME_FRAGMENT;
    if (np->common)
	flags |= LIT_NAME_COMMON;

    ret += lit_out_number (LIT_NAME);
    ret += lit_out_string (np->ident, strlen (np->ident));
    ret += lit_out_number (flags);

    return ret;
}

int
lit_out_marker (int area_type)
{
    static unsigned long oldpos = 0;
    unsigned long curpos = ftell (lit_out_file);
    int n = 0;

    assert (curpos >= oldpos);
    
    n += lit_out_number (LIT_MARKER);
    n += lit_out_number (area_type);
    n += lit_out_number (curpos - oldpos);

    assert (n <= LIT_MARKER_LENGTH);
    while (n++ < LIT_MARKER_LENGTH)
	lit_out_number (LIT_DUMMY);

    oldpos = curpos + LIT_MARKER_LENGTH;
    return LIT_MARKER_LENGTH;
}


/* Output expression in the reverse Polish form */

int
lit_out_expr (expr e)
{
    static int level = 0;
    int ret = 0;

    level++;

    if (EXPR_ISLEAF (e)) {
	ret += lit_out_number (e->tag);

	switch (e->tag) {
	case EXPR_CONST:
	    ret += lit_out_number (e->u.constval);
	    break;
	case EXPR_NAME:
	    assert (e->u.name->index >= 0);
	    ret += lit_out_number (e->u.name->index);
	    break;
	case EXPR_ELEMENT:
	    assert (e->u.element);
	    assert (e->u.element->index >= 0);
	    ret += lit_out_number (e->u.element->index);
	    break;
	case EXPR_ELSUM:
	    {
		expr *ep = &e->u.elsum.vector[0];
		expr *epe = &e->u.elsum.vector[e->u.elsum.count];

		for (; ep < epe; ep++) {
		    assert ((*ep)->u.element);
		    assert ((*ep)->u.element->index >= 0);
		    ret += lit_out_number ((*ep)->u.element->index);
		}
	    }
	    ret += lit_out_number (-1);
	    break;
	case EXPR_RANGE:
	    ret += lit_out_number (e->u.range.lo);
	    ret += lit_out_number (e->u.range.hi);
	    break;
	default:
	    abort ();
	}
    }
    else {
	if (! EXPR_ISUNARY (e))
	    ret += lit_out_expr (e->u.ptr.l);
	ret += lit_out_expr (e->u.ptr.r);
	ret += lit_out_number (e->tag);
    }

    if (--level == 0)
	ret += lit_out_number (LIT_EXPR_END);

    return ret;
}
