/* $Id: ments.h,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#ifndef MENTS_H
#define MENTS_H

/*
 * This file relates to in-core intermediate representation of the
 * program being assembled.
 */

#include "shared.h"

/* The representation overlook.

   There is a number of `struct segment's, each pointed to by its
   respective name (`val' field).  The segment names are stored in
   the `segment_names' hashtable, so it is easy to find a segment
   structure given a segment name.

   All commands/data ever emitted in a segment can be found via its
   `struct segment'.  They are represented as linked chains of
   elements, every chain representing a single fragment.  Element
   chains are pointed to by the `val' field of fragment names;
   fragment names are stored together with most other names in the
   `general_names' hashtable.  Besides, `struct segment' carries an
   array of names of all fragments seen in the segment.

   `struct segment' also contains a pointer to the last element of the
   last fragment, which makes emitting new data easy.  */


/* There is one instance of this structure for every segment
   encountered in the input. */

typedef struct segment *segment;
struct segment {

    /* The name of this segment.  The `val' field of the name points
       back to the segment structure. */
    name name;

    /* Vector of all fragments encountered so far.  The allocated
       size for `fragments' is always a minimal power of 2 greater
       than nfragments. */
    struct fraglist {
	name name;		/* the fragment's name */
	struct element *elts;	/* doubly-linked element chain */
    } *fragments;
    int nfragments;

    struct element *curel;	/* the last element in current fragment */
    expr point;			/* current assemble offset in this segment */

    int slot;			/* if nonzero, forced size the next command */
    int block_threshold;	/* Constant DATA elements smaller than
				   this are converted into BLOCKs in
				   pass_unroll */
#ifdef AST
    /* delayed alignment: we don't know yet if it should be applied to
       the current fragment or to the new fragment started by a label
       immediately after the .align. */
    int pending_alignment;

    /* the value to fill with */
    expr pending_alignment_filler;
#endif /* AST */
};


typedef struct element *element;
struct element {
    struct element *next;
    struct element *prev;

    /* Shows what kind of element it is, and hence which member of the
       union below applies */
    enum element_tag {
	ELEMENT_BLOCK,
	ELEMENT_DATA,
	ELEMENT_CMD,
    } tag;

    /* An expression showing the size of this thing in target's
       memory; most often an EXPR_RANGE */
    expr size;

    union {
	/* ELEMENT_BLOCK: a completely assembled something: a plain
	   byte sequence.  All other elements represent something
	   which cannot be assembled yet because of dependency on
	   another something which is not yet (well) defined. */
	struct {
	    char *ptr;		/* where the byte sequence is kept */
	    size_t allocated;	/* size of the malloced area */
	} block;

	/* ELEMENT_DATA */
	struct {
	    int width;
	    expr value;
	    expr count;
	} data;

	/* ELEMENT_CMD: */
	struct {
	    unsigned char opcode; /* one of TOK_CMD_DIRECT* opcodes */
	    expr arg;		/* the command operand */
	} cmd;
    } u;

    int index;			/* index of this element in the object file */
    int nrefs;			/* number of EXPR_ELEMENTs referencing us */
    int xcount;			/* for use by substract_elsums */
};

/* Segment vector: segments in the order we saw them. */
extern segment *segments;
extern int nsegments;

segment add_segment (name segname, name deffrag);
element element_alloc (enum element_tag tag);
void element_free (element);
void add_element (segment, enum element_tag tag);
void add_fragment (segment, name newfrag);
void swap_last_fragments (segment seg);
void emit_block (segment, char *ptr, int len);
void emit_cmd (segment, int opcode, expr arg);
int emit_align (segment, int align, expr filler);
void emit_data (segment, int width, expr data, expr count);
char *element_block_make_space (element el, int len);
void segment_dump (FILE *stream, char *leader, segment seg);
void element_dump (FILE *stream, char *leader, element el);
long pass_optimize_offsets (void);
void pass_freeze (void);
void pass_reduce (void);
void pass_canonicalize (void);
void pass_unroll (void);
void pass_merge (void);
void pass_needs (void);
void pass_output_segments (void);
void pass_count_refs (void);

#endif /* not MENTS_H */
