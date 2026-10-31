/* $Id: ments.c,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#include "shared.h"


/* Segment vector: segments in the order we saw them */

segment *segments = NULL;
int nsegments = 0;


/* Allocate and initialize a new `segment' structure for the segment
   with name `np'.  Setup np->val to point to the segment.

   Note: segment names are only used to find segments when a segment
   switch directive is encountered.  They never appear in expressions. */

segment
add_segment (name np, name deffrag)
{
    segment seg = (segment) xmalloc (sizeof (*seg));

    if (VECTOR_FULL (8, nsegments))
	VECTOR_GROW (8, nsegments, segment, segments);
    segments[nsegments++] = seg;

    np->val = (expr) seg;

    seg->name = np;
    seg->fragments = NULL;
    seg->nfragments = 0;
    seg->curel = NULL;
    seg->point = NULL;
    seg->slot = 0;
    seg->block_threshold = DEFAULT_BLOCK_THRESHOLD;
#ifdef AST
    seg->pending_alignment = 1;
    seg->pending_alignment_filler = NULL;
#endif /* AST */

    add_fragment (seg, deffrag);

    return seg;
}

element
element_alloc (enum element_tag tag)
{
    element el = (element) xmalloc (sizeof (*el));

    /* Initialize common fields of the element.  Union members of the
       element remain uninitialized; the caller should handle them. */

    el->tag = tag;
    if (tag == ELEMENT_BLOCK) {
	el->size = expr_const (0);	/* caller will grow this */
	el->u.block.ptr = NULL;
	el->u.block.allocated = 0;
    }
    else
	el->size = NULL;		/* caller should set this */

    el->index = -1;	/* illegal, to be reset or caught */
    el->nrefs = 0;	/* no refs yet */

    return el;
}

void
element_free (element el)
{
    EXPR_FREE (el->size);

    switch (el->tag) {
    case ELEMENT_BLOCK:
	if (el->u.block.ptr)
	    free (el->u.block.ptr);
	break;
    case ELEMENT_DATA:
	EXPR_FREE (el->u.data.value);
	EXPR_FREE (el->u.data.count);
	break;
    case ELEMENT_CMD:
	EXPR_FREE (el->u.cmd.arg);
    default:
	abort ();
    }
}

/* Add new element to the element chain of the current fragment in
   segment `seg'.  */

void
add_element (segment seg, enum element_tag tag)
{
    element el = element_alloc (tag);

    assert (seg->nfragments);	/* add_segment should ensure there
				   is a default fragment here */

    /* Link the new el into the current fragment's element chain.  The
       first el in the chain is pointed to by the fragment name's
       `val' field, all other els are pointed to by their respective
       preceding els. */

    if (seg->curel)
	seg->curel->next = el;
    else
	seg->fragments[seg->nfragments-1].elts = el;

    el->next = NULL;
    el->prev = seg->curel;
    seg->curel = el;
}


/* Switch segment `seg' to the new fragment `newfrag'. */
 
void
add_fragment (segment seg, name newfrag)
{
    if (VECTOR_FULL (16, seg->nfragments))
	VECTOR_GROW (16, seg->nfragments, struct fraglist, seg->fragments);
    seg->fragments[seg->nfragments].name = newfrag;
    seg->fragments[seg->nfragments].elts = NULL;
    seg->nfragments++;
    if (seg->point)
	EXPR_FREE (seg->point);
    seg->point = expr_name (newfrag);
    seg->curel = NULL;
}

void
swap_last_fragments (segment seg)
{
    struct fraglist temp;

    temp = seg->fragments[seg->nfragments-2];
    seg->fragments[seg->nfragments-2] = seg->fragments[seg->nfragments-1];
    seg->fragments[seg->nfragments-2] = temp;
}

/*
 * Emit functions add new elements of all sorts
 * to the curent chain of elements.
 */

/* Emit a plain byte sequence. */

void
emit_block (segment seg, char *ptr, int len)
{
    if (! seg->curel || seg->curel->tag != ELEMENT_BLOCK)
	add_element (seg, ELEMENT_BLOCK);
    memcpy (element_block_make_space (seg->curel, len), ptr, len);
    seg->curel->size = expr_unshare (seg->curel->size);
    seg->curel->size->u.constval += len;
    expr_plus_constant (&seg->point, len);
}


/* Emit a command with the given opcode and argument */

void
emit_cmd (segment seg, int opcode, expr arg)
{
    if (EXPR_ISCONST (arg)) {
	int sz;

	if (! seg->curel || seg->curel->tag != ELEMENT_BLOCK)
	    add_element (seg, ELEMENT_BLOCK);
	sz = cmd_generate (element_block_make_space (seg->curel, CMD_MAXLEN),
			   opcode, arg->u.constval, seg->slot);
	if (sz < 0) {
	    error ("slot %d too narrow for the command\n", seg->slot);
	    sz = 0;
	}
	expr_plus_constant (&seg->curel->size, sz);
	expr_plus_constant (&seg->point, sz);
    }
    else {
	expr temp;
	int minsz, maxsz;
	int a,b;

	add_element (seg, ELEMENT_CMD);
	seg->curel->u.cmd.opcode = opcode;
	seg->curel->u.cmd.arg = expr_share (arg);

	temp = expr_estimate (arg);

	switch (temp->tag) {
	case EXPR_CONST:
	    minsz = maxsz = cmd_size (temp->u.constval);
	    break;
	case EXPR_RANGE:
	    /* Beware -- the command size as a function of the argument
	       if not monotonous: command_size(MIN_INT,MAX_INT) is not 8,
	       but rather [1..8].  So (1) if the range includes "0" then
	       minsize is 1 and (2) maxsize is a maximum of sizes at
	       the range boundaries. */
	    a = cmd_size (temp->u.range.lo);
	    b = cmd_size (temp->u.range.hi);
	    if (temp->u.range.lo <= 0 && temp->u.range.hi >= 0)
		minsz = 1;
	    else
		minsz = a<b? a: b;
	    maxsz = a>b? a: b;
	    break;
	default:
	    abort ();
	}
	EXPR_FREE (temp);

	if (seg->slot) {
	    if (minsz > seg->slot)
		error ("slot %d too narrow for the command\n", seg->slot);
	    minsz = maxsz = seg->slot;
	}

	seg->curel->size = ((minsz == maxsz)? expr_const (minsz):
			    expr_range (minsz, maxsz));
	expr_plus_element (&seg->point, seg->curel);
    }
    seg->slot = 0;
}

/* Emit something to make the point aligned at the boundary specified
   by `align'.  Return 0 if nothing needs to be emitted, 1 otherwise.  */

int
emit_align (segment seg, int align, expr filler)
{
    int shift;

    assert (align != 0);
    assert (seg->point);

    /* First, see if we can do without making an ALIGN element */
    shift = expr_check_alignment (seg->point, align);

    if (shift == 0)
	return 0;			/* already aligned properly */

    if (shift < align && EXPR_ISCONST (filler)) {
	/* not aligned but well-based */
	if (! seg->curel || seg->curel->tag != ELEMENT_BLOCK)
	    add_element (seg, ELEMENT_BLOCK);
	memset (element_block_make_space (seg->curel, align - shift),
		filler->u.constval, align - shift);
	seg->curel->size = expr_unshare (seg->curel->size);
	seg->curel->size->u.constval += align - shift;
	expr_plus_constant (&seg->point, align - shift);
	return 1;
    }

    /* do it in the general way */

    add_element (seg, ELEMENT_DATA);
    seg->curel->u.data.width = 1;
    seg->curel->u.data.value = expr_share (filler);
    /* the count of bytes to be emitted to compliment point to a
       multiple of `align' is intuitively written as
       ((align-(point&(align-1))) & (align-1)), which boils down to
       ((align-point)&(align-1)) */
    seg->curel->u.data.count
	= expr_binop_owned (EXPR_AND,
			    expr_binop_owned (EXPR_MINUS,
					      expr_const (align),
					      expr_share (seg->point)),
			    expr_const (align-1));

    seg->curel->size = expr_range (0, align-1);
    expr_plus_element (&seg->point, seg->curel);

    return 1;
}


void
emit_data (segment seg, int width, expr data, expr count)
{
    expr temp;
    expr uemp;

    /* Don't bother too much trying to glue the data to the current
       element; the merit of doing so is not clear until we know what
       comes next.  So now we only optimize the obvious case of
       current element being the same data as that we are going to
       emit, and build a new element in all other cases.  We will
       merge worthy elements later, after the offset optimization.  */

    if (seg->curel
	&& seg->curel->nrefs == 0    /* otherwise we must not change the elt */
	&& seg->curel->tag == ELEMENT_DATA
	&& seg->curel->u.data.width == width
	&& expr_equal (seg->curel->u.data.value, data)) {

	assert (EXPR_ISCONST (seg->curel->size));
	expr_plus_constant (&seg->point, -seg->curel->size->u.constval);

	expr_asgop (EXPR_PLUS, &seg->curel->u.data.count, count);
	uemp = expr_binop (EXPR_MUL, seg->curel->u.data.count,
			   temp = expr_const (width));
	EXPR_FREE (seg->curel->size);
	seg->curel->size = expr_estimate (uemp);
	EXPR_FREE (temp);
	EXPR_FREE (uemp);

	expr_plus_element (&seg->point, seg->curel);
	return;
    }

    add_element (seg, ELEMENT_DATA);
    seg->curel->u.data.width = width;
    seg->curel->u.data.value = expr_share (data);
    seg->curel->u.data.count = expr_share (count);

    uemp = expr_binop (EXPR_MUL, count, temp = expr_const (width));
    seg->curel->size = expr_estimate (uemp);
    EXPR_FREE (temp);
    EXPR_FREE (uemp);

    expr_plus_element (&seg->point, seg->curel);
}

/* Make sure there are at least `len' unoccupied bytes in the area
   malloced for block.  Return the pointer to the first unoccupied
   byte. */

char *
element_block_make_space (element el, int len)
{
    assert (el->tag == ELEMENT_BLOCK);
    assert (el->size->tag == EXPR_CONST);

    if (el->size->u.constval + len > el->u.block.allocated) {
	if (el->u.block.allocated == 0)
	    el->u.block.allocated = 32;

	while (el->size->u.constval + len > el->u.block.allocated)
	    el->u.block.allocated *= 2;

	el->u.block.ptr = (char *) xrealloc (el->u.block.ptr,
					     el->u.block.allocated);
    }

    return &el->u.block.ptr[el->size->u.constval];
}

/* Dump the whole segment in readable form for debugging purposes */

void
segment_dump (FILE *stream, char *leader, segment seg)
{
    int i;

    fprintf (stream, "%ssegment \"%s\", %d fragments, point ",
	     leader, seg->name->ident, seg->nfragments);
    if (seg->point)
	expr_print (stream, seg->point);
    else
	fprintf (stream, "freed");
    fprintf (stream, "\n");

    for (i=0; i < seg->nfragments; i++) {
	name frag = seg->fragments[i].name;
	name *needp = frag->needs;
	element el;

	fprintf (stream, "%s  fragment \"%s\", align %d\n",
		 leader, frag->ident, frag->align);
	if (needp) {
	    for ( ; *needp; needp++)
		fprintf (stream, "%s    needs \"%s\"\n",
			 leader, (*needp)->ident);
	}
	for (el = seg->fragments[i].elts; el; el = el->next)
	    element_dump (stream, leader, el);
    }
}

/* for GDB */

void
print_segment (segment seg)
{
    segment_dump (stderr, "", seg);
}

/* Dump the element in readable form for debugging purposes */

void
element_dump (FILE *stream, char *leader, element el)
{
    int i;

    switch (el->tag) {
    case ELEMENT_BLOCK:
	assert (el->size->tag == EXPR_CONST);

	fprintf (stream, "%s    %p nrefs %d BLOCK(%d/%d)", leader, el,
		 el->nrefs, (int) el->size->u.constval, el->u.block.allocated);

	for (i=0; i < el->size->u.constval; i++) {
	    if (i % 16 == 0)
		fprintf (stream, "\n%s      0x%04x  ", leader, i);
	    fprintf (stream, "%02x%c", (unsigned char) el->u.block.ptr[i],
		     (i % 16 == 7) ? '-': ' ');
	}
	fprintf (stream, "\n");
	return;
	
    case ELEMENT_CMD:
	fprintf (stream, "%s    %p nrefs %d CMD 0x%02x",
		 leader, el, el->nrefs, el->u.cmd.opcode);
	fprintf (stream, "\n%s      arg ", leader);
	expr_print (stream, el->u.cmd.arg);
	fprintf (stream, "\n%s      size ", leader);
	expr_print (stream, el->size);
	fprintf (stream, "\n");
	return;

    case ELEMENT_DATA:
	fprintf (stream, "%s    %p nrefs %d DATA/%d",
		 leader, el, el->nrefs, el->u.data.width);
	fprintf (stream, "\n%s      value ", leader);
	expr_print (stream, el->u.data.value);
	fprintf (stream, "\n%s      count ", leader);
	expr_print (stream, el->u.data.count);
	fprintf (stream, "\n%s      size ", leader);
	expr_print (stream, el->size);
	fprintf (stream, "\n");
	break;

    default:
	abort ();
    }
}

/* for GDB */

void
print_element (element el)
{
    element_dump (stderr, "", el);
}


/* Optimization pass: travel through all element chains in all
   segments, recomputing loosely defined elements to get better
   estimation of their size.  Return the number of elements whose size
   has been changed. */

long
pass_optimize_offsets ()
{
    long nshrinks = 0;
    int i,j;

    for (i=0; i < nsegments; i++) {
	segment seg = segments[i];

	for (j=0; j < seg->nfragments; j++) {
	    name frag = seg->fragments[j].name;
	    int maxsz, minsz;
	    int a,b;
	    element el;
	    int elno = 0;
	    expr temp;

	    for (el = seg->fragments[j].elts; el; el = el->next, elno++) {

		if (EXPR_ISCONST (el->size))
		    continue;	/* already quite simple */

		/* recompute the size range of this element */
		switch (el->tag) {
		case ELEMENT_CMD:
		    expr_reduce (el->u.cmd.arg);
		    temp = expr_estimate (el->u.cmd.arg);

		    switch (temp->tag) {
		    case EXPR_CONST:
			minsz = maxsz = cmd_size (temp->u.constval);
			break;
		    case EXPR_RANGE:
			a = cmd_size (temp->u.range.lo);
			b = cmd_size (temp->u.range.hi);
			if (temp->u.range.lo <= 0 && temp->u.range.hi >= 0)
			    minsz = 1;
			else
			    minsz = a<b? a: b;
			maxsz = a>b? a: b;
			break;
		    default:
			abort ();
		    }
		    EXPR_FREE (temp);
		    break;
		    
		case ELEMENT_DATA:
		    expr_reduce (el->u.data.count);
		    temp = expr_estimate (el->u.data.count);
		    switch (temp->tag) {
		    case EXPR_CONST:
			/* We allow users write arbitrary count expressions,
			   but cannot do if they turn out negative */
			if (temp->u.constval < 0)
			    error ("DATA element %p has negative count\n", el);
			minsz = maxsz = temp->u.constval * el->u.data.width;
			break;
		    case EXPR_RANGE:
			minsz = temp->u.range.lo * el->u.data.width;
			maxsz = temp->u.range.hi * el->u.data.width;
			break;
		    default:
			abort ();
		    }
		    EXPR_FREE (temp);
		    break;
		    
		default:
		case ELEMENT_BLOCK: /* should always be of constant size */
		    abort ();
		}

		assert (el->size->tag == EXPR_RANGE);

		/* if recomputed size does not match the old size,
		   write the new size into el->size and count the
		   shrinkage */
		if (el->size->u.range.lo != minsz
		    || el->size->u.range.hi != maxsz) {

		    /* ranges should never grow */
		    assert (minsz >= el->size->u.range.lo);
		    assert (maxsz <= el->size->u.range.hi);

		    nshrinks++;

		    if (traceset & TRACE_OPT2) {
			printf ("** OPT2 seg %d \"%s\" fr %d \"%s\" el %d:  was ",
				i, seg->name->ident, j, frag->ident, elno);
			expr_print (stdout, el->size);
		    }

		    if (minsz == maxsz) {
			/* the size is constant now.  Convert to
			   EXPR_CONST _in place_ */
			el->size->tag = EXPR_CONST;
			el->size->u.constval = minsz;
		    }
		    else {
			el->size->u.range.lo = minsz;
			el->size->u.range.hi = maxsz;
		    }

		    if (traceset & TRACE_OPT2) {
			printf ("  now ");
			expr_print (stdout, el->size);
			printf ("\n");
		    }
		}
	    }
	}
    }

    return nshrinks;
}

/* Get rid of RANGE expressions in element->size's, when we are not
   going to optimize anymore.  */

#ifdef LIT
void
pass_freeze ()
{
    int i,j;

    for (i=0; i < nsegments; i++) {
	segment seg = segments[i];

	for (j=0; j < seg->nfragments; j++) {
	    element el;

	    for (el = seg->fragments[j].elts; el; el = el->next) {
		if (! EXPR_ISCONST (el->size)) {
		    switch (el->tag) {
		    case ELEMENT_BLOCK:
			/* nonconstant size block? */
			abort ();

		    case ELEMENT_DATA:
			/* We should not fix data size at higher
                           bound, as it (a) is not correct if this
                           DATA is a resut of .align and (b) can be
                           wasteful with some size-unknown-until-load
                           data.  However, doing so might cause
                           nonconstant segment sizes... well, if
                           people write such programs, they'll have to
                           find a way to handle loading.  */
			{
			    expr temp;
			    EXPR_FREE (el->size);
			    el->size = expr_binop (EXPR_MUL, el->u.data.count,
						   temp = expr_const (el->u.data.width));
			    EXPR_FREE (temp);
			}
			break;

		    case ELEMENT_CMD:
			{
			    expr e = el->size;
			    assert (e->tag == EXPR_RANGE);
			    e->tag = EXPR_CONST;
			    e->u.constval = e->u.range.hi;
			}
			break;
		    }
		}
	    }
	}
    }
}

/* Reduction pass: travel through elements, reducing every expression
   encountered.  */

void
pass_reduce ()
{
    int i,j;

    for (i=0; i < nsegments; i++) {
	segment seg = segments[i];

	for (j=0; j < seg->nfragments; j++) {
	    element el;

	    for (el = seg->fragments[j].elts; el; el = el->next) {

		expr_reduce (el->size);

		switch (el->tag) {
		case ELEMENT_BLOCK:
		    break;
		case ELEMENT_DATA:
		    expr_reduce (el->u.data.value);
		    expr_reduce (el->u.data.count);
		    break;
		case ELEMENT_CMD:
		    expr_reduce (el->u.cmd.arg);
		    break;
		}
	    }
	}
    }
}
#endif /* LIT */


#ifdef AST

/* Subroutine of pass_canonicalize: check if a [half]word consists of
   equal bytes */

static int
byteable (expr_const_t word, int width)
{
    int b = (unsigned char) word;
    int i;

    for (i=0; i < width; i++)
	if ((unsigned char) (word >>= 8) != b)
	    return 0;

    return 1;
}


/* Canonicalization pass: travel through all element chains in all
   segments, doing some transformations to DATA elements to make life
   easier for subsequent passes. */

void
pass_canonicalize ()
{
    int i,j;

    for (i=0; i < nsegments; i++) {
	segment seg = segments[i];

	for (j=0; j < seg->nfragments; j++) {
	    element el;

	    for (el = seg->fragments[j].elts; el; el = el->next) {
		if (el->tag == ELEMENT_DATA) {

		    /* Canonicalization: change things like
		       word:0x05050505*1 to byte:05*4 */

		    expr_reduce (el->u.data.value);
		    if (EXPR_ISCONST (el->u.data.value)
			&& el->u.data.width > 1) {
			if (byteable (el->u.data.value->u.constval,
				      el->u.data.width)) {
			    expr temp;
			    expr uemp;

			    temp = expr_binop
				(EXPR_MUL, el->u.data.count,
				 uemp = expr_const (el->u.data.width));

			    EXPR_FREE (uemp);
			    EXPR_FREE (el->u.data.count);
			    el->u.data.count = temp;
			    el->u.data.width = 1;
			    /* Might mask off the higher bytes of the
                               value, but why bother? */
			}
		    }
		}
	    }
	}
    }
}
#endif /* AST*/


/* Data merge and unrolling: travel through all element chains in all
   segments, merging similar neighbouring DATA elements.  Also,
   constant CMDs and DATA are transformed into BLOCKs ("unrolled") if
   it seems worthy */

void
pass_unroll ()
{
    int i,j;

    for (i=0; i < nsegments; i++) {
	segment seg = segments[i];

	for (j=0; j < seg->nfragments; j++) {
	    element el;

	    for (el = seg->fragments[j].elts; el; el = el->next) {

		switch (el->tag) {
		case ELEMENT_BLOCK:
		    /* no handling for BLOCKs in this pass */
		    break;

		case ELEMENT_DATA:
		    /* If next element is a similar data, increase the
		       count in this element and remove the next one.
		       But we may not do this if either of elements is
		       referenced from expressions, because this
		       opration changes size of the first element and
		       deletes the second. */
		    while (el->nrefs == 0
			   && el->next
			   && el->next->nrefs == 0
			   && el->next->tag == ELEMENT_DATA) {
			element nl = el->next;

			if (expr_equal (el->u.data.value, nl->u.data.value)
			    && el->u.data.width == nl->u.data.width) {

			    expr temp;

			    temp = expr_binop (EXPR_PLUS, el->u.data.count,
					       nl->u.data.count);
			    EXPR_FREE (el->u.data.count);
			    el->u.data.count = temp;

			    temp = expr_binop (EXPR_PLUS, el->size, nl->size);
			    EXPR_FREE (el->size);
			    el->size = expr_estimate (temp);
			    EXPR_FREE (temp);

			    el->next = nl->next;
			    if (nl->next)
				nl->next->prev = el;
			    element_free (nl);
			}
			else
			    break;
		    }

		    /* If the [probably increased by merging] DATA
                       element is not too large, turn it into a BLOCK */
		    
		    if (EXPR_ISCONST (el->u.data.value)
			&& EXPR_ISCONST (el->u.data.count)
			&& (el->u.data.count->u.constval * el->u.data.width
			    < seg->block_threshold)) {
			int value = el->u.data.value->u.constval;
			int count = el->u.data.count->u.constval;
			int width = el->u.data.width;
			char *p;
			int i,j;
			
			/* el->size not changes on DATA->BLOCK transition */
			EXPR_FREE (el->u.data.value);
			EXPR_FREE (el->u.data.count);
			el->tag = ELEMENT_BLOCK;
			el->u.block.ptr = NULL;
			el->u.block.allocated = 0;

			/* Grow the block to el->size; kinda trick */
			element_block_make_space (el, 0);
			p = el->u.block.ptr;

			for (i=0; i < count; i++) {
			    for (j=0; j < width; j++) {
#ifdef TARGET_LITTLE_ENDIAN
				*p++ = value >> 8*j;
#else
				abort ();
#endif
			    }
			}
		    }
		    break;

		case ELEMENT_CMD:
		    /* If argument is a constant, convert to BLOCK */
		    expr_reduce (el->u.cmd.arg);
		    if (EXPR_ISCONST (el->u.cmd.arg)) {
			int opcode = el->u.cmd.opcode;
			int arg = el->u.cmd.arg->u.constval;
			int sz;

			/* el->size not changes on CMD->BLOCK transition */
			EXPR_FREE (el->u.cmd.arg);
			el->tag = ELEMENT_BLOCK;
			el->u.block.ptr = NULL;
			el->u.block.allocated = 0;
			element_block_make_space (el, CMD_MAXLEN);

			sz = cmd_generate (el->u.block.ptr, opcode, arg,
					   el->size->u.constval);
			if (sz < 0)
			    error ("element %p: slot %d is too narrow\n",
				   el, el->size->u.constval);
		    }
		    break;

		default:
		    abort ();
		}
	    }
	}
    }
}

/* Travel through all element chains in all segments, merging
   neighbouring BLOCK elements. */

void
pass_merge ()
{
    int i,j;

    for (i=0; i < nsegments; i++) {
	segment seg = segments[i];

	for (j=0; j < seg->nfragments; j++) {
	    element el;

	    for (el = seg->fragments[j].elts; el; el = el->next) {

		switch (el->tag) {
		case ELEMENT_BLOCK:
		    /* If the next element is also a block, move its
                       data to the current one.  Again, we may not do
                       this if either of elements is referenced from
                       expressions, because this opration changes size
                       of the first element and deletes the second. */
		    while (el->next
			   && el->nrefs == 0
			   && el->next->nrefs == 0
			   && el->next->tag == ELEMENT_BLOCK) {
			element nl = el->next;
			int n = nl->size->u.constval;

			memcpy (element_block_make_space (el, n),
				nl->u.block.ptr, n);

			expr_plus_constant (&el->size, n);

			el->next = nl->next;
			if (nl->next)
			    nl->next->prev = el;
			element_free (nl);
		    }
		    break;

		case ELEMENT_DATA:
		case ELEMENT_CMD:
		    break;

		default:
		    abort ();
		}
	    }
	}
    }
}


/* Assign indices to loosely defined elements and record needs for
   every fragment. */

void
pass_needs ()
{
    int i,j;
    int index = 0;

    for (i=0; i < nsegments; i++) {
	segment seg = segments[i];

	for (j=0; j < seg->nfragments; j++) {
	    name frag = seg->fragments[j].name;
	    element el;

	    for (el = seg->fragments[j].elts; el; el = el->next) {

		switch (el->tag) {
		case ELEMENT_BLOCK:
		    el->index = index++;
		    break;

		case ELEMENT_DATA:
		    el->index = index++;
		    record_needs_name = frag;
		    if (! EXPR_ISCONST (el->u.data.value))
			record_needs (el->u.data.value);
		    if (! EXPR_ISCONST (el->u.data.count))
			record_needs (el->u.data.count);
		    break;

		case ELEMENT_CMD:
		    el->index = index++;
		    record_needs_name = frag;
		    record_needs (el->u.cmd.arg);
		    break;

		default:
		    abort ();
		}
	    }
	}
    }
}

/* Write all segments contents to the output file, thus forming the
   SEGMENTS area of the file */

void
pass_output_segments ()
{
    int i,j;

    for (i=0; i < nsegments; i++) {
	segment seg = segments[i];

	/* Output a segment switch record for all segments except the
           default one */
	if (i) {
	    lit_out_number (LIT_SEGMENT);
	    lit_out_string (seg->name->ident, strlen (seg->name->ident));
	    lit_out_number (seg->fragments[0].name->index);
	}

	for (j=0; j < seg->nfragments; j++) {
	    name frag = seg->fragments[j].name;
	    element el;

	    /* Output a fragment switch record for all fragments
	       except the default one */
	    if (j) {
		lit_out_number (LIT_FRAGMENT);
		lit_out_number (frag->index);
		lit_out_number (frag->align);
	    }

	    for (el = seg->fragments[j].elts; el; el = el->next) {

		switch (el->tag) {
		case ELEMENT_BLOCK:
		    lit_out_string (el->u.block.ptr, el->size->u.constval);
		    break;
		case ELEMENT_DATA:
		    lit_out_number (LIT_DATA);
		    lit_out_number (el->u.data.width);
		    lit_out_expr (el->u.data.value);
		    lit_out_expr (el->u.data.count);
		    lit_out_expr (el->size);
		    break;
		case ELEMENT_CMD:
		    lit_out_number (LIT_CMD);
		    lit_out_number (el->u.cmd.opcode);
		    lit_out_expr (el->u.cmd.arg);
		    lit_out_expr (el->size);
		    break;
		default:
		    abort ();
		}
	    }
	}
    }

    lit_out_marker (LIT_AREA_SEGMENTS);
}


/* Debugging pass: count references in all expressions attached to elements */

#ifdef EXPR_DEBUG
void
pass_count_refs ()
{
    int i,j;

    for (i=0; i < nsegments; i++) {
	segment seg = segments[i];

	for (j=0; j < seg->nfragments; j++) {
	    element el;

	    for (el = seg->fragments[j].elts; el; el = el->next) {

		if (el->size)
		    expr_count_refs (el->size);

		switch (el->tag) {
		case ELEMENT_BLOCK:
		    break;
		case ELEMENT_DATA:
		    if (el->u.data.value)  expr_count_refs (el->u.data.value);
		    if (el->u.data.count)  expr_count_refs (el->u.data.count);
		    break;
		case ELEMENT_CMD:
		    if (el->u.cmd.arg)  expr_count_refs (el->u.cmd.arg);
		    break;
		}
	    }
	}
    }
}
#endif /* EXPR_DEBUG */
