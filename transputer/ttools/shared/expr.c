/* $Id: expr.c,v 1.3 1996/01/16 15:29:13 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

/* This file contains expression-handling functions */

#include "shared.h"


/* Memory allocation primitives to use for expression nodes.  With
   glibc, it hopefully won't be too bad to use plain malloc/free; with
   worse malloc implementations it may be worthy to go into
   maintaining a dedicated pool of free nodes. */
#define expr_node_alloc()	(xmalloc(sizeof (struct expr)))
#define expr_node_free(E)	(free(E))

/* In this file, we can use a shorter name for the tag-to-mask macro. */
#define M(TAG)  EXPR_TAG_MASK(TAG)

/* Minimal size for EXPR_ELSUM vectors */
#define ELSUM_MINSZ  16

/* Flag telling expr_print to prepend memory address to ambibuous
   terms, namely ranges and nonames.  Default is short. */
int expr_print_long = 0;

/* Depth of recursion in expr_reduce */
static int expr_reduce_depth = 0;

/*
 *
 */

static expr expr_glue_element (expr e, expr elexpr);
static expr_const_t eval (enum expr_tag, expr_const_t, expr_const_t);
static void expr_reduce_elsum (expr e);
static int expr_reduce_trivia (expr e);
static expr scan1 (expr e, enum expr_tag op);
static expr scan2 (expr e, enum expr_tag op);
static expr substract_elsums (expr e1, expr e2);
static expr expr_estimate1 (expr e);
static expr expr_estimate_elsum (expr e);
static void expr_print_unary (FILE *f, char *, expr, char *);
static void expr_print_binary (FILE *f, char *, expr, char *, expr, char *);
static int sum_overflows (expr_const_t a, expr_const_t b);

static expr scan_top;	/* static arg for scan1(), scan2() */
static expr scan_item;	/* static arg for scan2() */

#ifdef EXPR_DEBUG
/* all allocated exprs in a double linked ring */
struct expr expr_list_base = {
    &expr_list_base,	/* forw */
    &expr_list_base,	/* back */
};
expr expr_list = &expr_list_base;
#endif /* EXPR_DEBUG */

/*
 * Basic expression functions.
 */

/* Allocate and initialize an expression node */

expr
expr_alloc (enum expr_tag tag)
{
    expr e = expr_node_alloc ();

    e->tag = tag;
    e->nrefs = 1;
    e->reent = 0;
    e->reent2 = 0;

#ifdef EXPR_DEBUG
    if (traceset & (TRACE_EXPR|TRACE_EXPR2|TRACE_STAT)) {
	e->forw = expr_list->forw;
	e->back = expr_list;
	expr_list->forw->back = e;
	expr_list->forw = e;
    }
#endif /* EXPR_DEBUG */

    return e;
}

/* Delete the expression, recursively freeing memory occupied by it  */

void
expr_free (expr e)
{
    --e->nrefs;

    if (e->nrefs > 0)
	return;	/* There are other holders, shouldn't really free it. */

    if (! EXPR_ISLEAF (e)) {
	EXPR_FREE (e->u.ptr.r);
	if (! EXPR_ISUNARY (e))
	    EXPR_FREE (e->u.ptr.l);
    }
    else if (e->tag == EXPR_ELEMENT && e->u.element != NULL) {
	element el = e->u.element;
	assert (el->nrefs > 0);
	el->nrefs--;
    }
    else if (e->tag == EXPR_ELSUM && e->u.elsum.vector != NULL) {
	expr *ep = &e->u.elsum.vector[0];
	expr *epe = &e->u.elsum.vector[e->u.elsum.count];
	for (; ep < epe; ep++)
	    EXPR_FREE (*ep);
	free (e->u.elsum.vector);
    }

#ifdef EXPR_DEBUG
    if (traceset & (TRACE_EXPR|TRACE_EXPR2|TRACE_STAT)) {
	e->back->forw = e->forw;
	e->forw->back = e->back;
    }
#endif /* EXPR_DEBUG */

    expr_node_free (e);
}

/* Obtain an "official" expression node pointer, permitted for inclusion
   in other expressions.  */

expr
expr_share (expr e)
{
    e->nrefs++;
    return e;
}

/* Make sure the expression node pointed to by `e' is not shared by
   copying the node if it is shared currently.  This function must be
   called before modifying any expression node in place.

   NOTE: unlike most other functions, expr_unshare considers its
   argument an _owned_ expression.  You no longer own an expression
   you passed as an argument to expr_unshare.  So things like the
   following is prefectly legal: name->val = expr_unshare (name->val); */

expr
expr_unshare (expr e)
{
    if (e->nrefs > 1) {
	expr f = expr_alloc (e->tag);

	expr_node_copy (f, e);
	f->nrefs = 1;
	e->nrefs--;
	return f;
    }
    return e;
}

/* Node copying often used in reduction routines.  `from' is an
   arbitrary expression, considered non-owned, as usual.  `to' should
   be a node holding no references; for example, if it is an unary
   operator node, u.ptr.r must be freed before calling expr_node_copy. */

expr
expr_node_copy (expr to, expr from)
{
    to->tag = from->tag;
    to->u = from->u;

    /* Now increase nrefs in the subexpressions referred to by `from',
       if any */

    if (! EXPR_ISLEAF (from)) {
	from->u.ptr.r->nrefs++;
	if (! EXPR_ISUNARY (from))
	    from->u.ptr.l->nrefs++;
    }
    else if (from->tag == EXPR_ELEMENT) {
	/* we cannot handle zero u.element case here -- but I am
	   pretty sure this won't happen, because ELEMENT is a leaf,
	   and never needs unsharing. */
	assert (from->u.element);
	from->u.element->nrefs++;
    }
    else if (from->tag == EXPR_ELSUM && from->u.elsum.vector) {
	to->u.elsum.vector = NULL;
	if (to->u.elsum.count) {
	    /* Copy the vector */
	    int i;
	    /* figure the malloced size of the vector... unpleasant.
	       We scan *down* to the nearest grow point, and then ask
	       to grow the vector.  This should malloc enough space to
	       contain `count' entries. */
	    i = to->u.elsum.count - 1;
	    while (! VECTOR_FULL (ELSUM_MINSZ, i))
		i--;
	    VECTOR_GROW (ELSUM_MINSZ, i, expr, to->u.elsum.vector);
	    for (i=0; i < to->u.elsum.count; i++) {
		to->u.elsum.vector[i] = from->u.elsum.vector[i];
		to->u.elsum.vector[i]->nrefs++;
	    }
	}
    }

    return (to);
}

/* Like expr_node_copy, but `from' is freed after copying.  This may
   be more efficient than {expr_node_copy (x, y); expr_free (y);},
   especially on elsums. */

void
expr_node_move (expr to, expr from)
{
    if (from->nrefs > 1) {
	expr_node_copy (to, from);
	EXPR_FREE (from);
    }
    else {
	to->tag = from->tag;
	to->u = from->u;
#ifdef notyet
	to->last_reduced = from->last_reduced;
#endif
	/* Avoid freeing u.ptr.? or e.elsum.vector -- they continue to
	   live in `to' */
	from->tag = EXPR_CONST;
	EXPR_FREE (from);
    }
}


/* Compare two expressions for equality. */

int
expr_equal (expr e, expr f)
{
    if (e == f)  /* With shared expressions, this should be a frequent case. */
	return 1;

    if (e->tag != f->tag)
	return 0;

    switch (e->tag) {
    case EXPR_CONST:
	return e->u.constval == f->u.constval;

    case EXPR_NAME:
	return e->u.name == f->u.name;

    case EXPR_ELEMENT:
	return e->u.element == f->u.element;

    case EXPR_ELSUM:
	/* Comparing vectors is hard work.  Let's count them different
           unless they are the same expression; this shouldn't create
           unreduced expressions as elsums are always evaluable
           after pass_freeze. */
	return 0;
    case EXPR_RANGE:
	/* Ranges are NOT considered equal even if they appear so:
	   they are subject to change during offset optimization.  We
	   only can count e and f equal if they are the same
	   expression, but this is not the case if we got here.  */
	return 0;

    default:
	if (EXPR_ISUNARY (e))
	    return expr_equal (e->u.ptr.r, f->u.ptr.r);
	else
	    return (expr_equal (e->u.ptr.r, f->u.ptr.r)
		    && expr_equal (e->u.ptr.l, f->u.ptr.l));
    }
}


/*
 * Constructors.
 */

/* Build a constant expression with the value given.  */

expr
expr_const (expr_const_t val)
{
    expr e = expr_alloc (EXPR_CONST);
    e->u.constval = val;
    return e;
}

/* Build a name-pointer expression.  */

expr
expr_name (struct name *np)
{
    expr e = expr_alloc(EXPR_NAME);
    e->u.name = np;
    return e;
}

/* Build a range expression.  */

expr
expr_range (expr_const_t lo, expr_const_t hi)
{
    expr e = expr_alloc(EXPR_RANGE);
    e->u.range.lo = lo;
    e->u.range.hi = hi;
    return e;
}

/* Build an element expression.  */

expr
expr_element (struct element *el)
{
    expr e = expr_alloc(EXPR_ELEMENT);
    e->u.element = el;
    if (el) {	/* lit sometimes creates ELEMENT expressions without
		   the actual element available; don't segfault in
		   this case.  However, it is then their
		   responsibility to increase nrefs when attaching an
		   element here... */
	el->nrefs++;
    }
    return e;
}

/* Build an empty element vector expression */

expr
expr_elsum ()
{
    expr e = expr_alloc(EXPR_ELSUM);
    e->u.elsum.vector = NULL;
    e->u.elsum.count = 0;
    return (e);
}

void
expr_elsum_append (expr e, expr eel)
{
    assert (eel->tag == EXPR_ELEMENT);
    if (VECTOR_FULL (ELSUM_MINSZ, e->u.elsum.count))
	VECTOR_GROW (ELSUM_MINSZ, e->u.elsum.count, expr, e->u.elsum.vector);
    e->u.elsum.vector[e->u.elsum.count++] = expr_share (eel);
}


/* Build an expression which is the result of unary operation `op'
   over the operand `e1' */

expr
expr_unop (enum expr_tag op, expr e1)
{
    expr e;

    /* Avoid creating EXPR_NEG expressions, as they are just an
       unnecessary special case in reduction, etc.; create (0-e1)
       instead. */
    if (op == EXPR_NEG)
	return expr_binop_owned (EXPR_MINUS, expr_const (0), expr_share (e1));

    if (EXPR_ISCONST (e1))
	return expr_const (eval (op, 0, e1->u.constval));

    e = expr_alloc(op);
    e->u.ptr.r = expr_share (e1);
    return e;
}

/* Same as expr_unop, but considers its expression argument as *owned*
   expression, and frees it after the actual construction is done. */

expr
expr_unop_owned (enum expr_tag op, expr e1)
{
    expr e = expr_unop (op, e1);
    EXPR_FREE (e1);
    return e;
}

/* Build an expression which is a result of binary operation `op' over
   the operands `e1' and `e2'. */

expr
expr_binop (enum expr_tag op, expr e1, expr e2)
{
    expr e;

    if (EXPR_ISCONST (e1) && EXPR_ISCONST (e2))
	return expr_const (eval (op, e1->u.constval, e2->u.constval));

    /* Canonicalize: swap constant to the right */
    if (EXPR_TAG_ISCOMM (op) && EXPR_ISCONST (e1)) {
	e = e1;
	e1 = e2;
	e2 = e;
    }

    /* frequent special case: (x + C) + D -> x + (C+D) */

    if (op == EXPR_PLUS && e1->tag == EXPR_PLUS
	&& EXPR_ISCONST (e2) && EXPR_ISCONST (e1->u.ptr.r)) {

	e = expr_unshare (expr_share (e1));
	e->u.ptr.r = expr_unshare (e->u.ptr.r);
	e->u.ptr.r->u.constval += e2->u.constval;

	return e;
    }

    /* lift constant: (x + C) + y -> (x + y) + C.  Should buy a big
       gain later, when adding more constants.  But not if it requires
       real unsharing: trading in sharing for a probably future win
       wouldn't be a good deal. */

    if (op == EXPR_PLUS	&& e1->tag == EXPR_PLUS
	&& EXPR_ISCONST (e1->u.ptr.r)
	&& e1->nrefs == 1) {

	expr temp = e1->u.ptr.r;

	/* this is superfluous -- we checked e1->nrefs is 1.
	e1 = expr_unshare (e1);  */
	e1->u.ptr.r = expr_share (e2);

	e = expr_alloc(op);
	e->u.ptr.l = expr_share (e1);
	e->u.ptr.r = temp;

	return e;
    }

    e = expr_alloc(op);
    e->u.ptr.l = expr_share (e1);
    e->u.ptr.r = expr_share (e2);

    return e;
}

/* Same as expr_binop, but considers its expression arguments as
   *owned* expression, and frees them after the actual construction is
   done. */

expr
expr_binop_owned (enum expr_tag op, expr e1, expr e2)
{
    expr e = expr_binop (op, e1, e2);
    EXPR_FREE (e1);
    EXPR_FREE (e2);
    return e;
}

expr
expr_questop (expr cond, expr iftrue, expr ifnot)
{
    expr e;

    if (EXPR_ISCONST (cond))
	return expr_share (cond->u.constval? iftrue: ifnot);
    
    e = expr_alloc (EXPR_QUEST);
    e->u.ptr.l = expr_share (cond);
    e->u.ptr.r = expr_alloc (EXPR_COLON);
    e->u.ptr.r->u.ptr.l = expr_share (iftrue);
    e->u.ptr.r->u.ptr.r = expr_share (ifnot);

    return e;
}

/* Build an expression which is a result of `e1 op e2', storing the
   resultion expression pointere where `e1' formerly was.  */

void
expr_asgop (enum expr_tag op, expr *e1p, expr e2)
{
    expr e1 = *e1p;

    /* frequent special case: (x + C) += D -> x + (C+D) */

    if (op == EXPR_PLUS && e1->tag == EXPR_PLUS
	&& EXPR_ISCONST (e2) && EXPR_ISCONST (e1->u.ptr.r)) {
	e1 = *e1p = expr_unshare (e1);
	e1->u.ptr.r = expr_unshare (e1->u.ptr.r);
	e1->u.ptr.r->u.constval += e2->u.constval;
	return;
    }

    /* another case: (x + E) - E -> x */
    if (op == EXPR_MINUS && e1->tag == EXPR_PLUS
	&& expr_equal (e1->u.ptr.r, e2)) {
	e1 = *e1p = expr_unshare (e1);
	expr_node_copy (e1, e1->u.ptr.l);
	return;
    }
  
    /* else do it in the general way */
    e1 = expr_binop (op, e1, e2);
    EXPR_FREE (*e1p);
    *e1p = e1;
}

/* Ad hoc function to add a constant to an expression in place */

void
expr_plus_constant (expr *ep, expr_const_t toadd)
{
    expr e = *ep;

    if (e->tag == EXPR_PLUS && EXPR_ISCONST (e->u.ptr.r)) {
	e = *ep = expr_unshare (e);
	e->u.ptr.r = expr_unshare (e->u.ptr.r);
	e->u.ptr.r->u.constval += toadd;
	return;
    }
    else { /* have to do it in the general way */
	expr temp;
	e = expr_binop (EXPR_PLUS, e, temp = expr_const (toadd));
	EXPR_FREE (*ep);
	EXPR_FREE (temp);
	*ep = e;
    }
}

void
expr_plus_element (expr *ep, element el)
{
    expr temp;
    expr uemp;

    if (EXPR_ISCONST (el->size)) {
	expr_plus_constant (ep, el->size->u.constval);
	return;
    }

    temp = expr_element (el);
    if ((uemp = expr_glue_element (*ep, temp))) {
	EXPR_FREE (*ep);
	*ep = uemp;
	EXPR_FREE (temp);
	return;
    }

    /* Could not glue -- go the standard way. */

    expr_asgop (EXPR_PLUS, ep, temp);
    EXPR_FREE (temp);
}


/* Try to append `elexpr' to an ELSUM or ELEMENT expression possibly
   found in `e'.  Return replacement expression if succesful, NULL if
   no ELSUMs or ELEMENTs found in `e'. */

static expr
expr_glue_element (expr e, expr elexpr)
{
    if (e->tag == EXPR_ELSUM) {
	e = expr_unshare (expr_share (e));
	expr_elsum_append (e, elexpr);
	return e;
    }
    else if (e->tag == EXPR_ELEMENT) {
	expr temp = expr_elsum ();

	expr_elsum_append (temp, e);
	expr_elsum_append (temp, elexpr);
	return temp;
    }
    else if (e->tag == EXPR_PLUS) {
	expr temp;

	if ((temp = expr_glue_element (e->u.ptr.l, elexpr)))
	    return expr_binop_owned (EXPR_PLUS, temp, expr_share (e->u.ptr.r));
	if ((temp = expr_glue_element (e->u.ptr.r, elexpr)))
	    return expr_binop_owned (EXPR_PLUS, expr_share (e->u.ptr.l), temp);
    }

    return NULL; /* cannot glue */
}

/*
 * Reduction stuff
 */

/* Attempt to simplify `e' using algebra.  The top expression node is
   modified in place, so all owners of the expression will benefit
   from the reduction.

   Return 1 if the reduction proved possible, 0 otherwise.

   Note that expr_reduce eliminates intermediate names in expressions,
   which means that alignment info the names might carry is lost.
   Therefore, expr_check_alignment has much worse chances on
   expr_reduce'd expressions. */

int
expr_reduce (expr e)
{
    int reduced = 0;

    if (traceset & TRACE_EXPR2)
	expr_fsck ();
    if (traceset & TRACE_REDUCE) {
	expr_reduce_depth++;
	printf ("** REDUCE %*sin ", (expr_reduce_depth-1) * 2, "");
	expr_print (stdout, e);
	printf ("\n");
    }

    assert  (! e->reent);	/* catch cycles in expression graph */
    e->reent = 1;

    if (e->tag == EXPR_ELSUM) {
	expr_reduce_elsum (e);
	/* Reducing elsum can result in arbitrary expression, so go
           through further steps. */
    }

    /* If it is a name with an expression in the `val' field, lift
       that expression here and go on reducing.  But do not if this is
       a global name -- it may be redefined in link time! */
    if (e->tag == EXPR_NAME && e->u.name->val && ! e->u.name->global) {
	/* Reduce the name's value before lifting, so all other owners
           of the expression will benefit from the reduction. */
	expr_reduce (e->u.name->val);
	expr_node_copy (e, e->u.name->val);
	goto end;
    }

    /* If it is an element with well-defined size (as opposed to range),
       we can drop the element node, lifting the size expression. */
    if (e->tag == EXPR_ELEMENT && e->u.element->size->tag != EXPR_RANGE) {
	element el = e->u.element;
	expr_reduce (el->size);
	expr_node_copy (e, el->size);
	assert (el->nrefs > 0);
	el->nrefs--;
	goto end;
    }

    /* If it is (still) a leaf, there is nothing to simplify.  */
    if (EXPR_ISLEAF (e))
	goto end;

    /* recurse to reduce subtrees */
    if (! EXPR_ISUNARY (e))
	reduced += expr_reduce (e->u.ptr.l);
    reduced += expr_reduce (e->u.ptr.r);

    /* Process constants and try various operation-dependent things */
    reduced += expr_reduce_trivia (e);

    /* Perhaps we have reduced it to the simplest possible form? */
    if (EXPR_ISLEAF (e))
	goto end;

    /* Try to exploit associativity: substitute terms from right
       subexpression into the left one.  */
    if (M(e->tag) & (M(EXPR_PLUS) | M(EXPR_MINUS)
		     | M(EXPR_AND) | M(EXPR_XOR) | M(EXPR_OR))) {
	expr newr, newl;

	scan_top = expr_share (e->u.ptr.l);
	newr = scan1 (e->u.ptr.r, e->tag);
	newl = scan_top;  /* possibly modified by scan1 */

	EXPR_FREE (e->u.ptr.l);
	EXPR_FREE (e->u.ptr.r);

	if (! newl && ! newr) {
	    e->tag = EXPR_CONST;
	    e->u.constval = 0;
	}
	else if (! newl) {
	    if (e->tag == EXPR_MINUS) {
		e->u.ptr.l = expr_const (0);
		e->u.ptr.r = newr;
	    }
	    else
		expr_node_move (e, newr);
	}
	else if (! newr)
	    expr_node_move (e, newl);
	else {
	    e->u.ptr.l = newl;
	    e->u.ptr.r = newr;
	}
    }

 end:
    e->reent = 0;

    if (traceset & TRACE_REDUCE) {
	printf ("** REDUCE %*sout ", (expr_reduce_depth-1) * 2, "");
	expr_print (stdout, e);
	printf ("\n");
	expr_reduce_depth--;
    }
    if (traceset & TRACE_EXPR2)
	expr_fsck ();

    return reduced;
}


static void
expr_reduce_elsum (expr e)
{
    expr sum = NULL;
    expr *ep = &e->u.elsum.vector[0];
    expr *epe = &e->u.elsum.vector[e->u.elsum.count];
    expr *ep2 = ep;

    assert (e->tag == EXPR_ELSUM);

    for (; ep < epe; ep++) {
	if ((*ep)->tag == EXPR_ELEMENT) {
	    if ((*ep)->u.element->size->tag != EXPR_RANGE) {
	        if (! sum)
		    sum = expr_share ((*ep)->u.element->size);
		else
		    expr_asgop (EXPR_PLUS, &sum, (*ep)->u.element->size);
		EXPR_FREE ((*ep));
	    }
	    else
		*ep2++ = *ep;
	}
	else {
	    /* Yes -- it can be not en element already, as a result of
	       reduction of the EXPR_ELEMENT that used to be here as a
	       part of some other expr */
	    if (! sum)
	        sum = expr_share (*ep);
	    else
		expr_asgop (EXPR_PLUS, &sum, (*ep));
	    EXPR_FREE (*ep);
	}
    }

    /* have we squeezed out something? */
    if (ep2 == &e->u.elsum.vector[0]) {
	/* ... everything! */
        expr_node_move (e, sum);
    }
    else if (ep2 < ep) {	/* anything? */
	/* ... yes, but some EXPR_ELEMENTS left.  Turn the whole
	   thing into (modified_elsum + sum) */
	expr e1 = expr_elsum ();
	e1->u.elsum.vector = e->u.elsum.vector;
	e1->u.elsum.count = ep2 - &e->u.elsum.vector[0];
	e->tag = EXPR_PLUS;
	e->u.ptr.l = e1;
	e->u.ptr.r = sum;
    }
}

/* Try on `e' various reduction rules that do not require do descend
   to subexpressions.  Return nonzero if managed to reduce.  */

static int
expr_reduce_trivia (expr e)
{
    /* If operands are constants, we can evaluate.  */

    if (EXPR_ISCONST (e->u.ptr.r)) {	/* BTW this excludes e->tag==QUEST */
	if (EXPR_ISUNARY (e)) {
	    expr_const_t temp = eval (e->tag, 0, e->u.ptr.r->u.constval);
	    EXPR_FREE (e->u.ptr.r);
	    e->tag = EXPR_CONST;
	    e->u.constval = temp;
	    return 1;
	}
	if (EXPR_ISCONST (e->u.ptr.l)) {
	    expr_const_t temp = eval (e->tag, e->u.ptr.l->u.constval,
				      e->u.ptr.r->u.constval);
	    EXPR_FREE (e->u.ptr.l);
	    EXPR_FREE (e->u.ptr.r);
	    e->tag = EXPR_CONST;
	    e->u.constval = temp;
	    return 1;
	}
    }

    /* Canonicalize commutative expressions by swapping constant to RHS.
       && and || are considered commutative too, because we have no side
       effects.

       ??? Is the canonicalization really necessary? */

    if (EXPR_ISCOMM (e)
	&& EXPR_ISCONST (e->u.ptr.l)) {
	expr temp = e->u.ptr.l;
	e->u.ptr.l = e->u.ptr.r;
	e->u.ptr.r = temp;
    }

#if 0 /* scan* will do this, no need to duplicate it here */
    /* x - x => 0, for {-,^} */

    if ((e->tag == EXPR_MINUS || e->tag == EXPR_XOR)
	&& expr_equal (e->u.ptr.l, e->u.ptr.r)) {
	EXPR_FREE (e->u.ptr.l);
	EXPR_FREE (e->u.ptr.r);
	e->tag = EXPR_CONST;
	e->u.constval = 0;
	return 1;
    }
#endif

    /* Handle the cases where one of the operands is a constant
       invariant in respect to this node's operation */

    /* x + 0 => x, for {+,-,<<,>>,^,|,||} */

    if (M (e->tag) & (M (EXPR_MINUS) | M (EXPR_PLUS) | M (EXPR_LSH)
		      | M (EXPR_RSH) | M (EXPR_XOR) | M (EXPR_OR)
		      | M (EXPR_OROR))
	&& e->u.ptr.r->tag == EXPR_CONST
	&& e->u.ptr.r->u.constval == 0) {
	EXPR_FREE (e->u.ptr.r);
	expr_node_move (e, e->u.ptr.l);
	return 1;
    }

    /* x * 1 => x,  x / 1 => x */

    if ((e->tag == EXPR_MUL || e->tag == EXPR_DIV)
	&& e->u.ptr.r->tag == EXPR_CONST
	&& e->u.ptr.r->u.constval == 1) {
	EXPR_FREE (e->u.ptr.r);
	expr_node_move (e, e->u.ptr.l);
	return 1;
    }

    /* x * 0 => 0, for {*,&} */

    if ((e->tag == EXPR_MUL || e->tag == EXPR_AND)
	&& e->u.ptr.r->tag == EXPR_CONST
	&& e->u.ptr.r->u.constval == 0) {
	EXPR_FREE (e->u.ptr.l);
	EXPR_FREE (e->u.ptr.r);
	e->tag = EXPR_CONST;
	e->u.constval = 0;
	return 1;
    }
      
    /* x && non-0 => x */

    if (e->tag == EXPR_ANDAND
	&& e->u.ptr.r->tag == EXPR_CONST
	&& e->u.ptr.r->u.constval != 0) {
	EXPR_FREE (e->u.ptr.r);
	expr_node_move (e, e->u.ptr.l);
	return 1;
    }
  
    /* x & 0xffffffff => x */

    if (e->tag == EXPR_AND
	&& e->u.ptr.r->tag == EXPR_CONST
	&& e->u.ptr.r->u.constval == ~0) {
	EXPR_FREE (e->u.ptr.r);
	expr_node_move (e, e->u.ptr.l);
	return 1;
    }

    /* 0? x:y => y,  non-0? x:y => x */

    if (e->tag == EXPR_QUEST && EXPR_ISCONST (e->u.ptr.l)) {
	expr l = e->u.ptr.l;
	expr r = e->u.ptr.r;

	assert (r->tag == EXPR_COLON);
	expr_node_copy (e, e->u.ptr.l->u.constval ? r->u.ptr.l
			                          : r->u.ptr.r);
	EXPR_FREE (l);
	EXPR_FREE (r);
	return 1;
    }

    /* x / 0 => complain: "division by 0", then convert to x to avoid
       further swearing */

    if ((e->tag == EXPR_DIV || e->tag == EXPR_REM)
	&& e->u.ptr.r->tag == EXPR_CONST
	&& e->u.ptr.r->u.constval == 0) {

	error ("Divizion by zero in expression: ");
	expr_print (stderr, e);
	error ("\n");

	EXPR_FREE (e->u.ptr.r);
	expr_node_move (e, e->u.ptr.l);
	return 1;
    }

    /* things like (<aligned_name> + 1) & 3 => 1 */

    if (e->tag == EXPR_AND
	&& e->u.ptr.r->tag == EXPR_CONST) {
	expr_const_t n = e->u.ptr.r->u.constval+1;

	if ((n & (n-1)) == 0) {
	    expr_const_t offset = expr_check_alignment (e->u.ptr.l, n);
	    
	    if (offset < n) {	/* aligned enough */
		EXPR_FREE (e->u.ptr.l);
		EXPR_FREE (e->u.ptr.r);
		e->tag = EXPR_CONST;
		e->u.constval = offset;
		return 1;
	    }
	}
    }

    return 0;
}


/* scan1 and scan2 constitute an engine which tries to simplify an
   associative expression by matching subexpressions of the right
   subtree to subexpressions of the left subtree.

   scan1 walks the right subtree of the expression being simplified,
   only descending the nodes corresponding to the associative
   operation in question.  For every operand found, it invokes scan2
   on the left subtree of the original expression to attempt to
   associate the operand with an operand in the right subtree.

   Return: a pointer to the replacement expression; it is equal to the
   scan's argument if no subsitution was made.  NULL if the expression
   has been changed into a const zero (in respect to the operation in
   question).  If the return value is non-NULL, it is owned by the
   caller, no exception here. */

static expr
scan1 (expr e, enum expr_tag op)
{
    expr newl, newr;

    if (scan_top == NULL) {
	/* The left subtree has already been already reduced to
           nothing, no point in further scanning the right one */
	return expr_share (e);
    }

    if (e->tag == op || ((op == EXPR_PLUS || op == EXPR_MINUS)
			 && (e->tag == EXPR_PLUS || e->tag == EXPR_MINUS))) {

	/* This node is a compatible associative op -- go deeper. */

	assert (!EXPR_ISUNARY (e) && !EXPR_ISLEAF (e));

	newl = scan1 (e->u.ptr.l, op);
	newr = scan1 (e->u.ptr.r, (e->tag == EXPR_MINUS
				   ? (op == EXPR_MINUS
				      ? EXPR_PLUS
				      : EXPR_MINUS)
				   : op));

	if (newl == e->u.ptr.l && newr == e->u.ptr.r) {
	    EXPR_FREE (newl);
	    EXPR_FREE (newr);
	}
	else {
	    if (! newl)
		return newr;
	    else if (! newr)
		return newl;
	    else {
		expr newe = expr_binop_owned (e->tag, newl, newr);
		return newe;
	    }
	}
    }

    else {
	/* Now it is a subexpression that we will try to associate with
	   something in the left subtree of the top expression.
	   
	   Note that we *may* modify the node pointed to by scan_top in
	   place, no matter what its `nrefs' says: modifications of
	   expression as a whole are good for all owners. */

	expr newtopl;

	scan_item = e;
	newtopl = scan2 (scan_top, op);
	if (newtopl == scan_top)
	    EXPR_FREE (newtopl);
	else {
	    /* scan2 managed to apply our subexpression somewhere in the
	       left subtree, so we eliminate it from here. */
	    EXPR_FREE (scan_top);
	    scan_top = newtopl;
	    return NULL;
	}
    }

    return expr_share (e);	/* found nothing */
}

expr
scan2 (expr e, enum expr_tag op)
{
    expr newl, newr;

    if (e->tag == op || ((op == EXPR_PLUS || op == EXPR_MINUS)
			 && (e->tag == EXPR_PLUS || e->tag == EXPR_MINUS))) {

	/* This node is a compatible associative binary op -- go deeper. */

	assert (!EXPR_ISUNARY (e) && !EXPR_ISLEAF (e));

	newl = scan2 (e->u.ptr.l, op);
	if (newl == e->u.ptr.l)
	    EXPR_FREE (newl);
	else {
	    if (! newl) {
		if (e->tag == EXPR_MINUS)
		    return expr_unop (EXPR_NEG, e->u.ptr.r);
		else
		    return expr_share (e->u.ptr.r);
	    }
	    else {
		expr newe = expr_binop (e->tag, newl, e->u.ptr.r);
		EXPR_FREE (newl);
		return newe;
	    }
	}
	    
	newr = scan2 (e->u.ptr.r, (e->tag == EXPR_MINUS
				   ? (op == EXPR_MINUS
				      ? EXPR_PLUS
				      : EXPR_MINUS)
				   : op));
	if (newr == e->u.ptr.r)
	    EXPR_FREE (newr);
	else {
	    if (! newr)
		return expr_share (e->u.ptr.l);
	    else {
		expr newe = expr_binop (e->tag, e->u.ptr.l, newr);
		EXPR_FREE (newr);
		return newe;
	    }
	}
    }

    else {
	/* Now it is something to attempt to pair with the item found
	   by scan1 */

	if (EXPR_ISCONST (e) && EXPR_ISCONST (scan_item)) {
	    return expr_const (eval (op, e->u.constval,
				     scan_item->u.constval));
	}
	if (op == EXPR_MINUS || op == EXPR_XOR) {
	    if (expr_equal (e, scan_item))
		return NULL;
	}
	if (op == EXPR_MINUS && e->tag == EXPR_ELSUM
	    && scan_item->tag == EXPR_ELSUM) {
	    expr newe = substract_elsums (e, scan_item);

	    if (newe != e)
		return newe;
	}
    }

    return expr_share (e);	/* found nothing */
}

/* Calculate the result of substraction of two elsum expressions.
   Return the resulting expression, or e1 if the result would be a
   trivial (e1 - e2), i.e. elsums do not intersect, or NULL if the
   result is 0.

   We use an O(N) algorithm for this, since elsums may be bulky. */

static expr
substract_elsums (expr e1, expr e2)
{
    expr *ep, *epe;
    int intersection_found = 0;

    /* Zero xcount fields in all elements referenced from either of
       the two elsums */
    ep = &e1->u.elsum.vector[0];
    epe = &e1->u.elsum.vector[e1->u.elsum.count];
    for (; ep < epe; ep++)
	if ((*ep)->tag == EXPR_ELEMENT)
	    (*ep)->u.element->xcount = 0;

    ep = &e2->u.elsum.vector[0];
    epe = &e2->u.elsum.vector[e2->u.elsum.count];
    for (; ep < epe; ep++)
	if ((*ep)->tag == EXPR_ELEMENT)
	    (*ep)->u.element->xcount = 0;

    /* Run through both elsums again, counting occuriences of every element */
    ep = &e1->u.elsum.vector[0];
    epe = &e1->u.elsum.vector[e1->u.elsum.count];
    for (; ep < epe; ep++)
	if ((*ep)->tag == EXPR_ELEMENT)
	    (*ep)->u.element->xcount++;

    ep = &e2->u.elsum.vector[0];
    epe = &e2->u.elsum.vector[e2->u.elsum.count];
    for (; ep < epe; ep++)
	if ((*ep)->tag == EXPR_ELEMENT)
	    if ((*ep)->u.element->xcount-- > 0)
		intersection_found = 1;

    if (! intersection_found)
	return e1;
    else {
	expr_const_t cnst = 0;
	expr newl = expr_elsum ();
	expr newr = expr_elsum ();
	expr newe;

	/* Build new, simplified rhs & lhs, as well as separate
           constant component */

	ep = &e1->u.elsum.vector[0];
	epe = &e1->u.elsum.vector[e1->u.elsum.count];
	for (; ep < epe; ep++) {
	    if ((*ep)->tag == EXPR_ELEMENT) {
		if ((*ep)->u.element->xcount > 0) {
		    (*ep)->u.element->xcount--;
		    expr_elsum_append (newl, *ep);
		}
	    }
	    else if ((*ep)->tag == EXPR_CONST)
		cnst +=	(*ep)->u.constval;
	    else
		abort ();
	}
	
	ep = &e2->u.elsum.vector[0];
	epe = &e2->u.elsum.vector[e2->u.elsum.count];
	for (; ep < epe; ep++) {
	    if ((*ep)->tag == EXPR_ELEMENT) {
		if ((*ep)->u.element->xcount < 0) {
		    (*ep)->u.element->xcount++;
		    expr_elsum_append (newr, *ep);
		}
	    }
	    else if ((*ep)->tag == EXPR_CONST)
		cnst -=	(*ep)->u.constval;
	    else
		abort ();
	}

	/* Form the resulting expression */
	if (newl->u.elsum.count != 0 && newr->u.elsum.count != 0) {
	    newe = expr_binop_owned (EXPR_MINUS, newl, newr);
	}
	else if (newl->u.elsum.count) {
	    newe = newl;
	    EXPR_FREE (newr);
	}
	else if (newr->u.elsum.count) {
	    newe = expr_unop_owned (EXPR_NEG, newr);
	    EXPR_FREE (newl);
	}
	else {
	    newe = NULL;
	    EXPR_FREE (newl);
	    EXPR_FREE (newr);
	}

	if (cnst != 0) {
	    if (newe)
		return expr_binop_owned (EXPR_PLUS, newe, expr_const (cnst));
	    else
		return expr_const (cnst);
	}

	return newe;
    }
}

/* See if `e' is a well-based expression, i.e. if we can tell what
   will be the result of (e & (align-1)).  Return the result if we can
   tell, `align' if we can not. */

int
expr_check_alignment (expr e, int align)
{
    assert (align != 0);

    switch (e->tag) {
    case EXPR_CONST:
	return e->u.constval & (align-1);

    case EXPR_NAME:
	if (e->u.name->align && e->u.name->align >= align)
	    return 0;
	if (e->u.name->val && ! e->u.name->global)
	    return expr_check_alignment (e->u.name->val, align);
	break;

    case EXPR_PLUS:
	{
	    expr_const_t loffset = expr_check_alignment (e->u.ptr.l, align);
	    expr_const_t roffset = expr_check_alignment (e->u.ptr.r, align);

	    if (loffset == align || roffset == align)
		break;

	    return (loffset + roffset) & (align-1);
	}

    case EXPR_MINUS:
	{
	    expr_const_t loffset = expr_check_alignment (e->u.ptr.l, align);
	    expr_const_t roffset = expr_check_alignment (e->u.ptr.r, align);

	    if (loffset == align || roffset == align)
		break;

	    return (loffset - roffset) & (align-1);
	}

    default:
	break;
    }

    /* Can't tell. */
    return align;
}

/* expr_estimate: Compute bounds on the value the expression 'e' will
   have after all loosely-defined terms inside it (names, ranges)
   become defined.  Returned is a constant expression if we were able
   to compute the value of `e', a range expression otherwise.  We do
   not call expr_reduce internally, so for best results you might want
   to reduce the expression before calling expr_estimate on it. */

expr
expr_estimate (expr e)
{
    expr temp = expr_estimate1 (e);

    assert (temp->tag == EXPR_RANGE);
    if (temp->u.range.lo == temp->u.range.hi) {
	expr uemp = expr_const (temp->u.range.lo);
	EXPR_FREE (temp);
	return uemp;
    }
    return temp;
}

/* Check if addition of a to b does not result in overflow; used by
   expr_estimate*.  This function is placed before functions that use
   it, so that GCC can inline it at higher optimization levels. */

static int
sum_overflows (expr_const_t a, expr_const_t b)
{
    if (a > 0 && b > 0 && a+b < 0)
	return 1;
    if (a < 0 && b < 0 && a+b >= 0)
	return 1;
    return 0;
}

/* expr_estimate1: Internal version of expr_estimate, different in
   that it always returns a RANGE expression, so we can get rid of
   expression type checking while walking the expression.  */

static expr
expr_estimate1 (expr e)
{
    expr temp, uemp, ret;

    switch (e->tag) {

    case EXPR_CONST:
	return expr_range (e->u.constval, e->u.constval);

    case EXPR_RANGE:
	return expr_share (e);

    case EXPR_ELEMENT:
	return expr_estimate1 (e->u.element->size);
	
    case EXPR_ELSUM:
	return expr_estimate_elsum (e);
	
    case EXPR_NAME:
	if (e->u.name->val == NULL || e->u.name->global)
	    break;  /* This should be a fragment -- we can't say
		       anything about its value prior to linking. */
	return expr_estimate1 (e->u.name->val);

    case EXPR_NEG:
	abort ();  /* should be turned into (0-e) */

    case EXPR_NOT:
	return expr_range (0, 1);

    case EXPR_COMPL:
	break;	/* why bother? */

    case EXPR_MINUS:
	temp = expr_estimate1 (e->u.ptr.r);
	if (temp->u.range.lo == EXPR_CONST_MIN) {
	    /* -EXPR_CONST_MIN == EXPR_CONST_MIN, you know...  so this
	       value cannot be represented by a single range */
	    EXPR_FREE (temp);
	    break;
	}
	uemp = expr_estimate1 (e->u.ptr.l);
	if (sum_overflows (uemp->u.range.lo, temp->u.range.lo)
	    || sum_overflows (uemp->u.range.hi, temp->u.range.hi)) {
	    EXPR_FREE (temp);
	    EXPR_FREE (uemp);
	    break;
	}
	ret = expr_range (uemp->u.range.lo - temp->u.range.hi,
			     uemp->u.range.hi - temp->u.range.lo);
	EXPR_FREE (temp);
	EXPR_FREE (uemp);
	return ret;
	
    case EXPR_PLUS:
	temp = expr_estimate1 (e->u.ptr.r);
	uemp = expr_estimate1 (e->u.ptr.l);
	if (sum_overflows (uemp->u.range.lo, temp->u.range.lo)
	    || sum_overflows (uemp->u.range.hi, temp->u.range.hi)) {
	    EXPR_FREE (temp);
	    EXPR_FREE (uemp);
	    break;
	}
	ret = expr_range (uemp->u.range.lo + temp->u.range.lo,
			     uemp->u.range.hi + temp->u.range.hi);
	EXPR_FREE (temp);
	EXPR_FREE (uemp);
	return ret;
	
    case EXPR_LSH:
    case EXPR_RSH:
    case EXPR_MUL:
    case EXPR_DIV:
    case EXPR_REM:
	break; 	/* later.... */

    case EXPR_AND:
	/* x & 0x0000000f -> [0...15] and suchlike */
	if (EXPR_ISCONST (e->u.ptr.r)) {
	    expr e2 = e->u.ptr.r;
	    if ((e2->u.constval & (e2->u.constval + 1)) == 0)
		return expr_range (0, e2->u.constval);
	}
	if (EXPR_ISCONST (e->u.ptr.l)) {
	    expr e2 = e->u.ptr.l;
	    if ((e2->u.constval & (e2->u.constval + 1)) == 0)
		return expr_range (0, e2->u.constval);
	}
	break;

    case EXPR_XOR:
    case EXPR_OR:
    case EXPR_LT:
    case EXPR_LE:
    case EXPR_GT:
    case EXPR_GE:
    case EXPR_EQ:
    case EXPR_NE:
    case EXPR_ANDAND:
    case EXPR_OROR:
    case EXPR_QUEST:
    case EXPR_COLON:
	break; 	/* later.... */
    default:
	abort ();
    }

    /* default is the widest possible range: we could not tell better... */
    return expr_range (EXPR_CONST_MIN, EXPR_CONST_MAX);
}

/* Estimate an EXPR_ELSUM, trying to be effective */

expr
expr_estimate_elsum (expr e)
{
    expr *ep, *epe;
    expr_const_t lo = 0;
    expr_const_t hi = 0;

    if (e->u.elsum.count == 0)
	return expr_range (0,0);

    ep = &e->u.elsum.vector[0];
    epe = &e->u.elsum.vector[e->u.elsum.count];

    for ( ; ep < epe; ep++) {
	if ((*ep)->tag == EXPR_ELEMENT) {
	    expr sze = (*ep)->u.element->size;

	    if (sze->tag == EXPR_RANGE) {
		if (sum_overflows (lo, sze->u.range.lo)
		    || sum_overflows (hi, sze->u.range.hi)) {
		    /* can't tell */
		    return expr_range (EXPR_CONST_MIN, EXPR_CONST_MAX);
		}
		lo += sze->u.range.lo;
		hi += sze->u.range.hi;
	    }
	    else if (sze->tag == EXPR_CONST) {
		lo += sze->u.constval;
		hi += sze->u.constval;
	    }
	    else
		abort ();
	}
	else if ((*ep)->tag == EXPR_CONST) {
	    lo += (*ep)->u.constval;
	    hi += (*ep)->u.constval;
	}
	else
	    abort ();
    }

    return expr_range (lo, hi);
}


/*
 * Constant expression evaluation.
 */

/* Return the result of operation specified by `tag' over the
   constants `lval' and `rval' */

static expr_const_t
eval (enum expr_tag tag, expr_const_t lval, expr_const_t rval)
{
    switch (tag) {
    case EXPR_CONST:
    case EXPR_NAME:
    case EXPR_QUEST:
    case EXPR_COLON:
    case EXPR_RANGE:
    case EXPR_ELEMENT:
    case EXPR_ELSUM:
    default:
	abort ();

    case EXPR_NEG:	return -rval;
    case EXPR_NOT:	return !rval;
    case EXPR_COMPL:	return ~rval;

    case EXPR_MINUS:	return lval - rval;
    case EXPR_PLUS:	return lval + rval;
    case EXPR_LSH:	return lval << rval;
    case EXPR_RSH:	return lval >> rval;
    case EXPR_MUL:	return lval * rval;

    case EXPR_DIV:
	if (rval == 0) {
	    error ("oops, division by 0 while evaluating expression");
	    return lval;
	}
	return lval / rval;

    case EXPR_REM:
	if (rval == 0) {
	    error ("oops, division by 0 while evaluating expression");
	    return lval;
	}
	return lval % rval;

    case EXPR_AND:	return lval & rval;
    case EXPR_XOR:	return lval ^ rval;
    case EXPR_OR:	return lval | rval;
    case EXPR_LT:	return lval < rval;
    case EXPR_LE:	return lval <= rval;
    case EXPR_GT:	return lval > rval;
    case EXPR_GE:	return lval >= rval;
    case EXPR_EQ:	return lval == rval;
    case EXPR_NE:	return lval != rval;
    case EXPR_ANDAND:	return lval && rval;
    case EXPR_OROR:	return lval || rval;
    }
}

/*
 * Debugging printout of expressions
 */

/* Wrapper of expr_print for GDB */

void
print_expr (expr e)
{
    expr_print (stderr, e);
    fprintf (stderr, "\n");
}

/* Print expression 'e' to stdio stream 'f' in more-or-less readable form */

void
expr_print (FILE *f, expr e)
{
    assert  (! e->reent2);	/* catch cycles in expression graph */
    e->reent2 = 1;

    if (expr_print_long >= 2)
	fprintf (f, "%p:", e);

    if (!e) {
	fprintf (f, "NULL-EXPRESSION-POINTER");
	return;
    }

    switch (e->tag) {

    default:
	abort ();

	/*
	 * Leaves
         */

    case EXPR_CONST:
	fprintf (f, "%"EXPR_CONST_FMT"", (long) e->u.constval);
	break;

    case EXPR_NAME:
	if (e->u.name->ident[0] != '\0')
	    fprintf (f, "%s", e->u.name->ident);
	else {  /* noname */
	    if (e->u.name->index != -1)
		fprintf (f, "\"#%d\"", e->u.name->index);
	    else if (expr_print_long >= 2)  /* address already printed */
		fprintf (f, "\"\"");
	    else
		fprintf (f, "\"%p\"", e->u.name);
	}
	break;

    case EXPR_ELEMENT:
	if (e->u.element == NULL)
	    fprintf (f, "[NULL]");
	else if (e->u.element->index == -1)
	    fprintf (f, "[%p]", e->u.element);
	else
	    fprintf (f, "[%d]", e->u.element->index);
	break;

    case EXPR_ELSUM:
	fprintf (f, "(SUM:");
	if (expr_print_long < 1 && e->u.elsum.count > 2)
	    fprintf (f, " %d members", e->u.elsum.count);
	else {
	    expr *ep = &e->u.elsum.vector[0];
	    expr *epe = &e->u.elsum.vector[e->u.elsum.count];

	    for ( ; ep < epe; ep++) {
		fprintf (f, " ");
		expr_print (f, *ep);
	    }
	}
	fprintf (f, ")");
	break;
	
	
    case EXPR_RANGE:
	if (expr_print_long >= 2)
	    fprintf (f, "%p:[%"EXPR_CONST_FMT"..%"EXPR_CONST_FMT"]",
		     e, (long) e->u.range.lo, (long) e->u.range.hi);
	else
	    fprintf (f, "[%"EXPR_CONST_FMT"..%"EXPR_CONST_FMT"]",
		     (long) e->u.range.lo, (long) e->u.range.hi);
	break;

	/*
	 * Unary ops
         */

    case EXPR_NEG:
	expr_print_unary (f, "(-", e->u.ptr.r, ")");
	break;
	
    case EXPR_NOT:
	expr_print_unary (f, "(!", e->u.ptr.r, ")");
	break;

    case EXPR_COMPL:
	expr_print_unary (f, "(~", e->u.ptr.r, ")");
	break;

	/*
	 * Binary ops
         */

    case EXPR_MINUS:
	expr_print_binary (f, "(", e->u.ptr.l, " - ", e->u.ptr.r, ")");
	break;

    case EXPR_PLUS:
	expr_print_binary (f, "(", e->u.ptr.l, " + ", e->u.ptr.r, ")");
	break;

    case EXPR_LSH:
	expr_print_binary (f, "(", e->u.ptr.l, " << ", e->u.ptr.r, ")");
	break;

    case EXPR_RSH:
	expr_print_binary (f, "(", e->u.ptr.l, " >> ", e->u.ptr.r, ")");
	break;

    case EXPR_MUL:
	expr_print_binary (f, "(", e->u.ptr.l, " * ", e->u.ptr.r, ")");
	break;

    case EXPR_DIV:
	expr_print_binary (f, "(", e->u.ptr.l, " / ", e->u.ptr.r, ")");
	break;

    case EXPR_REM:
	expr_print_binary (f, "(", e->u.ptr.l, " % ", e->u.ptr.r, ")");
	break;

    case EXPR_AND:
	expr_print_binary (f, "(", e->u.ptr.l, " & ", e->u.ptr.r, ")");
	break;

    case EXPR_XOR:
	expr_print_binary (f, "(", e->u.ptr.l, " ^ ", e->u.ptr.r, ")");
	break;

    case EXPR_OR:
	expr_print_binary (f, "(", e->u.ptr.l, " | ", e->u.ptr.r, ")");
	break;


    case EXPR_LT:
	expr_print_binary (f, "(", e->u.ptr.l, " < ", e->u.ptr.r, ")");
	break;

    case EXPR_LE:
	expr_print_binary (f, "(", e->u.ptr.l, " <= ", e->u.ptr.r, ")");
	break;

    case EXPR_GT:
	expr_print_binary (f, "(", e->u.ptr.l, " > ", e->u.ptr.r, ")");
	break;

    case EXPR_GE:
	expr_print_binary (f, "(", e->u.ptr.l, " >= ", e->u.ptr.r, ")");
	break;

    case EXPR_EQ:
	expr_print_binary (f, "(", e->u.ptr.l, " == ", e->u.ptr.r, ")");
	break;

    case EXPR_NE:
	expr_print_binary (f, "(", e->u.ptr.l, " != ", e->u.ptr.r, ")");
	break;

    case EXPR_ANDAND:
	expr_print_binary (f, "(", e->u.ptr.l, " && ", e->u.ptr.r, ")");
	break;

    case EXPR_OROR:
	expr_print_binary (f, "(", e->u.ptr.l, " || ", e->u.ptr.r, ")");
	break;

    case EXPR_QUEST:
	expr_print_binary (f, "(", e->u.ptr.l, " ? ", e->u.ptr.r, ")");
	break;

    case EXPR_COLON:
	expr_print_binary (f, "", e->u.ptr.l, " : ", e->u.ptr.r, "");
	break;
    }

    e->reent2 = 0;
}

static void
expr_print_unary (FILE *f, char *str_left, expr e1, char * str_right)
{
    fputs (str_left, f);
    expr_print (f, e1);
    fputs (str_right, f);
}

static void
expr_print_binary (FILE *f, char *str_left, expr e1,
                   char *str_mid, expr e2, char * str_right)
{
    fputs (str_left, f);
    expr_print (f, e1);
    fputs (str_mid, f);
    expr_print (f, e2);
    fputs (str_right, f);
}

/* Return 1 if the name `np' is mentioned somewhere in the expression
   `e', including expressions of other names referenced in `e'.  Useful
   to diagnose cycles in expressions. */

int
expr_name_mentioned (expr e, name np)
{
    if (! EXPR_ISLEAF (e))
	return (expr_name_mentioned (e->u.ptr.r, np)
		|| ((! EXPR_ISUNARY (e))
		    && expr_name_mentioned (e->u.ptr.l, np)));

    if (e->tag == EXPR_NAME) {
	if (e->u.name == np)
	    return 1;
	if (e->u.name->val)
	    return expr_name_mentioned (e->u.name->val, np);
    }

    return 0;
}

#ifdef EXPR_DEBUG
/* Increase refcount in `e' and all subexpressions of it */

void
expr_count_refs (expr e)
{
    if (e->refcount == 0) {
	if (! EXPR_ISLEAF (e)) {
	    expr_count_refs (e->u.ptr.r);
	    if (! EXPR_ISUNARY (e))
		expr_count_refs (e->u.ptr.l);
	}
	else if (e->tag == EXPR_ELSUM) {
	    expr *ep = &e->u.elsum.vector[0];
	    expr *epe = &e->u.elsum.vector[e->u.elsum.count];

	    for ( ; ep < epe; ep++)
		expr_count_refs (*ep);
	}
    }
    e->refcount++;
}

void *
name_count_refs (name np)
{
    if (np->val)
	expr_count_refs (np->val);
    return NULL;
}

char *expr_tag_name[] = {
  "CONST", "NAME",
  "NEG", "NOT", "COMPL",
  "MINUS", "PLUS",
  "LSH", "RSH",
  "MUL", "DIV", "REM",
  "AND", "XOR", "OR",
  "LT", "LE", "GT", "GE", "EQ", "NE",
  "ANDAND", "OROR",
  "QUEST", "COLON",
  "RANGE",
  "ELEMENT",
  "ELSUM"
};
#endif /* EXPR_DEBUG */

void
expr_statistics (char *header)
{
#ifdef EXPR_DEBUG
    int count[EXPR_TAG_COUNT];  /* count of expressions of each type */
    int reftotal[EXPR_TAG_COUNT];  /* total refs to exprs of each type */
    int refmax[EXPR_TAG_COUNT];  /* max nrefs seen for exprs of each type */
    int totalcount = 0;
    int i;
    expr e;

    memset (count, 0, sizeof (count));
    memset (reftotal, 0, sizeof (reftotal));
    memset (refmax, 0, sizeof (refmax));

    for (e = expr_list->forw; e != expr_list; e = e->forw) {
	count[e->tag]++;
	reftotal[e->tag] += e->nrefs;
	if (e->nrefs > refmax[e->tag])
	    refmax[e->tag] = e->nrefs;
    }

    for (i = 0; i < EXPR_TAG_COUNT; i++) {
	if (count[i] == 0)
	    continue;
	printf ("** STAT at %s: %s: %d nodes, max nrefs %d, avg nrefs %d.%d\n",
		header,	expr_tag_name[i], count[i], refmax[i],
		reftotal[i]/count[i], (reftotal[i]*10/count[i])%10);
	totalcount += count[i];
    }
    printf ("** STAT at %s: %d nodes total\n", header, totalcount);
#endif /* EXPR_DEBUG */
}

