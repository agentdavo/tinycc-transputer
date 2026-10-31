/* $Id: litio.h,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#ifndef LITIO_H
#define LITIO_H

/* Definitions pertaining to the LIT object file format */

/* LIT format object file consists of records, each starting with a
   tag byte saying what kind of record it is.

   The overall structure of the file is as follows.

   name_table		// sets up the ident<->index correspondence
   MARKER
   needs		// a list of `name1 needs name2' relations
   MARKER
   segment1		// start-of-segment record with ident string
     fragment1		// start-of-fragment record with name index
       element1		// BLOCK/DATA/CMD elements
       ...
       elementN
     fragmentN
   ...
   segmentN
   MARKER
   name_definitions	// list of non_fragment_name=expression pairs
   MARKER
   stab1
   ...
   stabN
   MARKER

   Area end markers allow to locate any area without having to read
   through the entire file.  Markers have fixed length (are padded
   with dummy records if necessary), thus the last marker can be read
   at fixed offset from EOF.  The marker contains the length of the
   area it marks, which can be used to seek to the previous area's
   marker, and so on.
 */

/*
 * Record tags.
 */

/* Positive tags mean block records of this length.  All other records have
   negative tags, which occupy two bytes in encoded form */

#define LIT_DUMMY	(0) /* no-op record, empty */
#define LIT_DATA	(-1) /* NEEE: data element */
#define LIT_CMD		(-2) /* NEE: command element */
#define LIT_NAME	(-3) /* SN: nametable entry: ident, flags */
#define LIT_NEEDS	(-4) /* NN[N...]: need list */
#define LIT_SEGMENT	(-5) /* SN: segment start record */
#define LIT_FRAGMENT	(-6) /* NN: fragment start record */
#define LIT_NAMEDEF	(-7) /* NE: name=value for nonfragment names */
#define LIT_MARKER	(-8) /* NN: area end marker */
#define LIT_COMMENT	(-9) /* S: a comment string */

/* Flag bits in LIT_NAME */
#define LIT_NAME_DEFINED	(1)
#define LIT_NAME_FRAGMENT	(2)
#define LIT_NAME_COMMON		(4)

/* MARKER records are padded with LIT_DUMMY records to this length */

#define LIT_MARKER_LENGTH	(8)

/* Area type codes in MARKER records */

#define LIT_AREA_NAMETABLE	(0)
#define LIT_AREA_NEEDS		(1)
#define LIT_AREA_SEGMENTS	(2)
#define LIT_AREA_DEFINITIONS	(3)
#define LIT_AREA_STABS		(4)

/* Expression terminator pseudo-tag.  Terminating with -1 is too
   wasteful. */

#define LIT_EXPR_END		(250)


#ifndef NO_LIBC
extern FILE *lit_out_file;
extern char *lit_out_file_name;
extern FILE *lit_in_file;
extern char *lit_in_file_name;

extern int lit_in_string_len;	/* Implicit return from lit_in_string */

int lit_out_number (long number);
int lit_out_string (char *str, size_t len);
int lit_out_ntab_entry (name np);
int lit_out_marker (int area_type);
int lit_out_expr (expr e);

long lit_in_number (void);
char *lit_in_string (void);
char *lit_in_block (size_t len);
expr lit_in_expr (void);
char *lit_area_name (long area);

/* called in litin.c, must be supplied by user */
extern struct name *name_by_index (long index);
extern struct expr *element_by_index (long index);

#endif /* not NO_LIBC */
#endif /* not LITIO_H */
