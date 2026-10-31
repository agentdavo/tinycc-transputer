/* $Id: misc.h,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#ifndef MISC_H
#define MISC_H

#include "shared.h"

/* This structure controls behaviour of the functions error/warning/fatal.
   Default instance of this is provided which is initialized to zero,
   which causes the functions add nothing to the caller-supplied string
   except the error type.  One can change the global pointer `error_report'
   and have a different reporting style in some part of the program */

struct error_report {
  int *n_errors;	/* number of error messages issued so far */
  int *n_warnings;	/* number of warning messages issued so far */
  char *my_name;	/* how we are named on this system; printed by fatal() */
  char **file_name;	/* current source file */
  int *line_number_ptr;	/* current line number in the source file */
};

extern struct error_report *error_report;

/* in-core repersentation for program symbols */

typedef struct name *name;
struct name {
  char *ident;		/* the name proper */
  struct expr *val;	/* the value assigned to this symbol */
  int align;		/* `val' is required [or known] to be
			   equally divisable by `align' */
  int index;		/* this name index in the object file */
  name *needs;		/* NULL-terminated list of names this one needs */

#ifdef LIT
  struct ifile **source; /* file (or list of files) it is defined in */
  struct ifile **refs;   /* file (or list of files) which refer to this name */
  struct element *elts;  /* 1st elt of the fragment with this name */
#endif

  /* flags */
  unsigned global:1;	/* ast: visible to linker */
  unsigned defined:1;	/* ast: definition seen; i.e. not `extern'.
			   lit: seen in nametable in one of the files.
			   Not necessarily means that `val' is nonzero */
  unsigned fragment:1;	/* is a fragment name */
  unsigned common:1;	/* created by .comm directive */
  unsigned wanted:1;	/* lit: selected to link in */
  unsigned multisrc:1;	/* lit: defined in more than one file */
  unsigned multiref:1;	/* lit: referred from more than one file */
  unsigned special:2;	/* lit: defined by linker (*@@{start,size}) */
  unsigned loadtime:1;	/* lit: visible to loader (*@@start) */
};

/* Values for np->special */
enum { NAME_SPECIAL_START=1, NAME_SPECIAL_SIZE, NAME_SPECIAL_BLOCKSIZE };

/* A simple array-based hash table */

struct table {
  int ncolumns;		/* length of top array; 0 if not initialized */
  name **top;		/* pointers to sublevels or single names */
  int *hassublevel;	/* bit array: is top[N] a name or sublevel? */
  name *nonames;	/* noname symbols are kept in this separate array,
			   or else they would all fell into one column. */
  int nnonames;		/* how many nonames are currently stored here */
};

/*
 * Dynamically reallocated vectors handling stuff.
 * 
 * Vectors are a basic storage facility we use here and there.  A vector
 * consists of a pointer to a malloced area and a counter of elements
 * currently stored in the area.  There is no separate variable to show
 * the size of the area; we always use the nearest power-of-two
 * for realloc, and so we can tell from COUNT what the malloced size
 * is.  More exactly, to avoid thrashing with small sizes, we use the
 * following size sequence:
 * 
 *   0  MINSIZE  MINSIZE*2  MINSIZE*4  MINSIZE*8  ...
 * 
 * Note that you MUST use a power-of-2 for MINSIZE; this allows to keep
 * the implementation of VECTOR_FULL simple.  You are also responsible
 * for initializing vector and count variables with zeroes.
 *
 */

#define VECTOR_FULL(MINSIZE, COUNT) \
  ((COUNT) == 0 || (COUNT) == MINSIZE || ((COUNT) & ((COUNT)-1)) == 0)

#define VECTOR_GROW(MINSIZE, COUNT, ELEM_TYPE, VECTOR) \
  ((VECTOR) = (ELEM_TYPE *) xrealloc (VECTOR, \
   sizeof (ELEM_TYPE) * ((COUNT) < (MINSIZE)? (MINSIZE): (COUNT)*2)))

/* Name tables */
extern struct table segment_names;	/* namespace for text/data/bss/... */
extern struct table general_names;	/* the common namespace */
extern struct table file_names;		/* nonstandard use: input filename
					   strings */

/* Bitsets enabling various trace and dump facilities */
extern int traceset;
extern int dumpset;

struct trace_tab {
    char *id;
    int mask;
};

/* Static arguments to record_needs() */
extern name record_needs_name;		/* name to add needs for */

/* This is a set of masks for `traceset' available in both ast and lit */
enum shared_trace_mask {
    TRACE_REDUCE = 0x40000000,	/* show exprs before and after expr_reduce */
    TRACE_REDUCE2 = 0x20000000,	/* show exprs before and after expr_reduce */
    TRACE_ESTIMATE = 0x10000000,/* trace expr_estimate() input/output */
    TRACE_OPT2 = 0x08000000,	/* show exprs before and after expr_reduce */
    TRACE_MISC = 0x04000000,	/* miscellaneous, minor, and rare */
    TRACE_EXPR = 0x02000000,	/* fsck expression graph */
    TRACE_EXPR2 = 0x01000000,	/* fsck expression graph more frequently */
    TRACE_ELSUMS = 0x00800000,	/* trace expr_form_elsums input/output */
    TRACE_STAT = 0x00400000,	/* trace expr_form_elsums input/output */
};

#define SHARED_TRACE_FLAGS \
    { "reduce", TRACE_REDUCE },		\
    { "reduce2", TRACE_REDUCE2 },	\
    { "opt2", TRACE_OPT2 },		\
    { "misc", TRACE_MISC },		\
    { "expr", TRACE_EXPR },		\
    { "expr2", TRACE_EXPR2 },		\
    { "estimate", TRACE_ESTIMATE },	\
    { "elsums", TRACE_ELSUMS },	\
    { "stat", TRACE_STAT },	\

void error (char *fmt, ...);
void error2 (char *file, int line, char *fmt, ...);
void verror (char *fmt, va_list ap);
void warning (char *fmt, ...);
void warning2 (char *file, int line, char *fmt, ...);
void vwarning (char *fmt, va_list ap);
void fatal (char *fmt, ...);
void *xmalloc (size_t);
void *xrealloc (void *, size_t);
char *xstrcat4 (char *s1, char *s2, char *s3, char *s4);
void table_init (struct table *, int);
name table_lookup (struct table *, char *, int);
name table_add_noname (struct table *);
void *table_walk (struct table *t, void * (*fcn) (name));
name name_alloc (char *, int);
void *name_free (name np);
void name_assign_value (name np, expr e);
void name_set_align (name np, int align);
void record_needs (expr e);
void add_need (name, name need);
int parse_trace_spec (char *spec, struct trace_tab *trace_tab, int *maskp);

/* User-defined */
void expr_fsck (void);

#endif /* not MISC_H */
