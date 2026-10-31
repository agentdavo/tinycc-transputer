/* $Id: misc.c,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#include "shared.h"

/* bits per int */
#define BPI  (sizeof (int) * 8)

/* bitset manipulation macros */
#define BIT_SET_SIZE(n)   (((n + BPI - 1) / BPI) * sizeof (int))
#define BIT_IN_SET(set, n)   (set[n / BPI] & (1<<(n % BPI)))
#define SET_BIT_IN_SET(set, n)   (set[n / BPI] |= (1<<(n % BPI)))
#define CLEAR_BIT_IN_SET(set, n)   (set[n / BPI] &= ~(1<<(n % BPI)))

/* Name tables */
struct table segment_names;	/* namespace for text/data/bss/... */
struct table general_names;	/* the common namespace */
struct table file_names;	/* nonstandard use: input filename strings */

/* Trace & dump flags */
int traceset = 0;		/* trace off by default */
int dumpset = 0;		/* dump off by default */

/* Static arguments to record_needs() */
name record_needs_name;		/* name to add needs for */

/* Default error report stuff */  
static int n_errors;
static int n_warnings;

static struct error_report default_error_report = {
  &n_errors,	/* n_errors */
  &n_warnings,	/* n_warnings */
  NULL,		/* my_name */
  NULL,		/* file_name */
  NULL,		/* line_number_ptr */
};

struct error_report *error_report = &default_error_report;


void
error (char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);

  verror (fmt, ap);
}

void
error2 (char *file, int line, char *fmt, ...)
{
  struct error_report save = *error_report;
  va_list ap;
  va_start (ap, fmt);

  error_report->file_name = &file;
  error_report->line_number_ptr = &line;

  verror (fmt, ap);  

  *error_report = save;
}

void
verror (char *fmt, va_list ap)
{
  if (error_report->n_errors)
      (*error_report->n_errors)++;

  if (error_report->file_name) {
      fprintf (stderr, "%s:", *error_report->file_name);
      if (error_report->line_number_ptr)
	  fprintf (stderr, "%u:", *error_report->line_number_ptr);
      fprintf (stderr, " ");
  }
  vfprintf(stderr, fmt, ap);
}


void
warning (char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);

  vwarning (fmt, ap);
}


void
warning2 (char *file, int line, char *fmt, ...)
{
  struct error_report save = *error_report;
  va_list ap;
  va_start (ap, fmt);

  error_report->file_name = &file;
  error_report->line_number_ptr = &line;

  vwarning (fmt, ap);  
   
  *error_report = save;
}

void
vwarning (char *fmt, va_list ap)
{
  if (error_report->n_warnings)
      (*error_report->n_warnings)++;

  if (error_report->file_name) {
      fprintf (stderr, "%s:", *error_report->file_name);
      if (error_report->line_number_ptr)
	  fprintf (stderr, "%u:", *error_report->line_number_ptr);
  }
  fprintf (stderr, "warning: ");
  vfprintf(stderr, fmt, ap);
}


void
fatal (char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);

  if (error_report->my_name)
    fprintf (stderr, "%s: ", error_report->my_name);

  fprintf (stderr, "fatal: ");

  vfprintf(stderr, fmt, ap);
  exit (1);
}


void *
xmalloc (size_t size)
{
    void *p = malloc (size);

    if (!p)
	fatal ("Memory exhausted -- that's the end.\n");

    return p;
}

void *
xrealloc (void *old, size_t size)
{
    void *p = realloc (old, size);

    if (!p)
	fatal ("Memory exhausted -- that's the end.");

    return p;
}

char *
xstrcat4 (char *s1, char *s2, char *s3, char *s4)
{
    int len = strlen (s1) + strlen (s2) + strlen (s3) + strlen (s4);
    char *p = (char *) xmalloc (len+1);

    strcpy (p, s1);
    strcat (p, s2);
    strcat (p, s3);
    strcat (p, s4);

    return p;
}



/* Initialize hash table to the said number of columns. */

void
table_init (struct table *t, int ncolumns)
{
    t->ncolumns = ncolumns;
    t->top = (name **) xmalloc (sizeof (void *) * ncolumns);
    t->hassublevel = (int *) xmalloc (BIT_SET_SIZE (ncolumns));
    memset (t->top, 0, sizeof (void *) * ncolumns);
    memset (t->hassublevel, 0, BIT_SET_SIZE (ncolumns));
    t->nonames = NULL;
    t->nnonames = 0;
}


/* Find the element with the given key in the table.  Add a new one if
   not found. */

name
table_lookup (struct table *t, char *key, int keylen)
{
    char *p = &key[keylen];
    int sum = 0;
    int column;

    if (keylen == 0)
	return table_add_noname (t);

    /* Compute hash function in a partially unrolled loop.  Not that
       this unrolling were really necessary, but for hacking
       pleasure...  hopefully it speeds up a bit.  */

    switch (keylen % 16) {
    case 0:
	while (p > key) {
	    sum += *--p;
    case 15: sum += *--p;
    case 14: sum += *--p;
    case 13: sum += *--p;
    case 12: sum += *--p;
    case 11: sum += *--p;
    case 10: sum += *--p;
    case 9: sum += *--p;
    case 8: sum += *--p;
    case 7: sum += *--p;
    case 6: sum += *--p;
    case 5: sum += *--p;
    case 4: sum += *--p;
    case 3: sum += *--p;
    case 2: sum += *--p;
    case 1: sum += *--p;
	}
    }

    column = sum % t->ncolumns;

    /* OK, we found the hash column.  Now we have three cases:
       1. unopened column (top[column] is NULL).
       2. column with a single element (hassublevel[column] is 0)
       3. full-fledged column.  top[column] points to a
          null-terminated sublevel array whose size is a power of 2.

       Find out which is our case and compare all elements in the
       column (if any) against the key.  */

    if (t->top[column]) {
	name *npp;
	
	if (BIT_IN_SET (t->hassublevel, column)) {
	    int nelements;
	    for (npp = t->top[column]; *npp; npp++) {
		if (memcmp (key, (*npp)->ident, keylen) == 0
		    && strlen ((*npp)->ident) == keylen)
		    return *npp;
	    }
	    /* not found. Is there room for new element? grow if nec.  */
	    nelements = npp - t->top[column];
	    if (VECTOR_FULL (4, nelements+1))
		VECTOR_GROW (4, nelements+1, name, t->top[column]);
	    npp = t->top[column] + nelements;
	    npp[0] = name_alloc (key, keylen);
	    npp[1] = NULL;
	    return npp[0];
	}
	else {
	    /* No sublevel; a single element in this column */
	    name np = (name) t->top[column];
	    if (memcmp (key, np->ident, keylen) == 0
		&& strlen (np->ident) == keylen)
		return np;
	    /* not found. Have to allocate sublevel */
	    t->top[column] = (name *) xmalloc (sizeof (name) * 4);
	    SET_BIT_IN_SET (t->hassublevel, column);
	    npp = t->top[column];
	    npp[0] = np;
	    npp[1] = name_alloc (key, keylen);
	    npp[2] = NULL;
	    return npp[1];
	}
    }

    /* The ease of adding the first element in a column is the most
       enjoying thing around. */

    ((name *) t->top)[column] = name_alloc (key, keylen);
    return (name) t->top[column];
}

name
table_add_noname (struct table *t)
{
    if (VECTOR_FULL (8, t->nnonames))
	VECTOR_GROW (8, t->nnonames, name, t->nonames);

    return t->nonames[t->nnonames++] = name_alloc ("", 0);
}

/* For every name in the table `t', call the function `fcn', passing
   the name as an argument.  The value returned by `fcn' controls
   further walking: if `fcn' returns non-zero, stop walking and return
   that value.  If all calls to `fcn' return zero, all table is walked
   through and zero is returned.  */

void *
table_walk (struct table *t, void * (*fcn) (name))
{
    int column = t->ncolumns;
    int n = t->nnonames;
    void *ret = NULL;

    while (--n >= 0) {
	if ((ret = (*fcn) (t->nonames[n])))
	    return ret;
    }

    while (--column >= 0) {
	if (t->top[column]) {
	    if (BIT_IN_SET (t->hassublevel, column)) {
		name *npp = t->top[column];
		while (*npp)
		    if ((ret = (*fcn) (*npp++)))
			return ret;
	    }
	    else {
		if ((ret = (*fcn) ((name) t->top[column])))
		    return ret;
	    }
	}
    }

    return ret;
}


/* Allocate and initialize a new name */

name
name_alloc (char *id, int len)
{
    name np = (name) xmalloc (sizeof (struct name));

    np->ident = (char *) xmalloc (len + 1);
    memcpy (np->ident, id, len);
    np->ident[len] = '\0';

    np->val = NULL;	/* no value */
    np->needs = NULL;	/* no needs */
#ifdef LIT
    np->source = NULL;	/* no sources yet */
    np->refs = NULL;	/* no refs yet */
    np->elts = NULL;	/* no associated elements yet */

    if (strcmp (&id[len-(sizeof("@@start")-1)], "@@start") == 0)
	np->special = NAME_SPECIAL_START;
    else if (strcmp (&id[len-(sizeof("@@size")-1)], "@@size") == 0)
	np->special = NAME_SPECIAL_SIZE;
    else if (strcmp (&id[len-(sizeof("@@blocksize")-1)], "@@blocksize") == 0)
	np->special = NAME_SPECIAL_BLOCKSIZE;
    else
	np->special = 0;
#endif
    np->align = 1;	/* unaligned */
    np->index = -1;	/* illegal, to be reset or caught */

    np->defined = 0;
    np->global = 0;
    np->fragment = 0;
    np->common = 0;
    np->wanted = 0;
    np->multisrc = 0;
    np->multiref = 0;

    return np;
}

void *
name_free (name np)
{
    if (np->val)
	EXPR_FREE (np->val);
    if (np->needs)
	free (np->needs);
#ifdef LIT
    if (np->source && np->multisrc)
	free (np->source);
#endif
    free (np->ident);
    free (np);

    return NULL;
}


/* Assign the value `e' to name `np', but first check that doing so
   does not make a cyclic expression. */

void
name_assign_value (name np, expr e)
{
    if (np->val)
	EXPR_FREE (np->val);

    if (expr_name_mentioned (e, np)) {
	error ("assignment of the value ");
	expr_print (stderr, e);
	fprintf (stderr, " to `%s' would make a cycle in expression graph\n",
		 np->ident);
	np->val = expr_const (0);
	return;
    }

    if (np->align > 1) {
	/* cannot assign arbitrary expression to this name
	   as it could break the alignment. */
	if (expr_check_alignment (e, np->align) != 0) {
	    error ("cannot assign the value ");
	    expr_print (stderr, e);
	    fprintf (stderr, " to \"%s\" because of its required alignment %d\n",
		     np->ident, np->align);
	    np->val = expr_const (0);
	    return;
	}
    }

    np->val = expr_share (e);
}

/* set alignment on the name ``np'', checking for its value (if any)
   being ok for the alignment requested */

void
name_set_align (name np, int align)
{
    if (align <= np->align)
	return;

    if (np->val) {
	if (expr_check_alignment (np->val, align) != 0) {
	    error ("cannot set the alignment %d on \"%s\" because of its seemingly unaligned value ",
		   align, np->ident);
	    expr_print (stderr, np->val);
	    fprintf (stderr, "\n");
	    return;
	}
    }
    np->align = align;
}



/* Walk `e', storing a need for every NAME mentioned in the need list for
   the name `record_needs_name' */

void
record_needs (expr e)
{
    if (EXPR_ISLEAF (e)) {
	if (e->tag == EXPR_NAME) {
	    assert (e->u.name->global); /* all others should have been
					   reduce()d away by now */
	    add_need (record_needs_name, e->u.name);
	}
    }
    else {
	record_needs (e->u.ptr.r);
	if (! EXPR_ISUNARY (e))
	    record_needs (e->u.ptr.l);
    }
}

/* Add new need `need' for name `name' */

void
add_need (name np, name need)
{
    int nneeds;

    if (np == need)
	return;		/* trivial self-need */

    if (np->needs == NULL)
	nneeds = 0;
    else {
	name *npp;
	/* See if we already have this dependency, so as not to
	   make duplicates */
	for (npp = np->needs; *npp; npp++) {
	    if (*npp == need)
		return;		/* already have this need */
	}
	nneeds = npp - np->needs;
    }

    if (VECTOR_FULL (2, nneeds + 1))
	VECTOR_GROW (2, nneeds + 1, name, np->needs);
    np->needs[nneeds] = need;
    np->needs[nneeds+1] = NULL;
}


/* Parse trace specification of the form key1[+key2][-key3][+key4]...
   The first key and all positive ones set bits in the trace mask,
   negative keys clear bits.  For example: all-lex-parse.

   Return positive on success, negative of spec is incorrect. */

int
parse_trace_spec (char *spec, struct trace_tab *trace_tab, int *maskp)
{
    int negative = 0;
    char *p = spec;

    for (;;) {
	int len = strcspn (p, "+-\0");
	struct trace_tab *tp;
	int found = 0;

	for (tp = trace_tab; tp->id; tp++) {
	    /* don't require exact match; prefix match is ok */
	    if (strncmp (tp->id, p, len) == 0) {
		if (negative)
		    *maskp &= ~tp->mask;
		else
		    *maskp |= tp->mask;
		found++;
	    }
	}
	if (! found)
	    return -1;
	p += len;
	if (*p == '\0')
	    break;
	negative = (*p == '-');
	p++;
    }

    return 1;
}
