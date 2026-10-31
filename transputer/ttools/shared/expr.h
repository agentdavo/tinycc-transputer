/* $Id: expr.h,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#ifndef EXPR_H
#define EXPR_H

#include "config.h"

/* This file specifies the representation of expressions in memory.
   Expression tags introduced here are also used in object and
   executable files.

   Expression is a means for representing a (probably non-constant)
   value, most often a target memory address.  Expressions are built
   of constants, name refererences, and operators.  The set of
   operators is borrowed from C.

   Expression is represented as a linked tree of nodes.  

   Subexpression sharing is allowed: a subexpression tree may be
   referred to from more than one expression.  This saves (hopefully)
   memory and time necessary to do the copying.  Sharing is controlled
   by means of reference counts kept in every node.  Subexpression by
   itself may be expr_reduce()d in place, so all superior expression
   will benefit from the reduction.  But if the subexpr needs to be
   modified in the course of reducing of a superior expression as a
   whole, the superior expr must be unshared from the modification
   point up to the top.

   Terminology:

   shared expression: expression with nrefs > 1
   proprietary expression: expression with nrefs == 1
   owned expression: expression [pointer] counted in nrefs.

   If you want to store an expression somewhere (e.g. np->val), you
   must own the expression.  On the other hand, if you own an
   expression, you must either store it somewhere or drop the
   ownership by calling expr_free on it.  Forgetting owned expressions
   is a memory leak.

   Most (all?) expr-valued functions return an owned expression.  You
   can also obtain ownership of any expression by calling expr_share
   on it; this makes you a co-owner of the expression, which thus
   becomes _shared_.

   Most functions consider their expression arguments non-owned
   expressions, so you don't need to expr_share them before the call
   and expr_free afterwards.  expr_unshare() is one exception from
   this; you lose the ownership of the expression you pass to
   expr_unshare (but as usual, you own the expression it returns).

   When an expression node needs to be modified, you should should
   first make sure it is referenced from only one place; use
   expr_unshare for this.  One exception from this rule is when the
   modificaton is due to an equivalent transformation on the
   expression, like those undertaken by expr_reduce.

   EXPR_RANGE expressions are used to approximate the values that
   cannot be expressed precisely in an acyclic expression graph.  To
   be more specific, to represent the estimated size of processor commands
   which depends on a number of things, on itself in particular.

   Huge expressions of the form (....)+elt)+elt)+elt)+elt) are very
   common on large programs, and take vast amount of time being
   processed the usual way -- twice a pity since such things are not
   reducible.  On the other hand, they need not be reduced since
   expr_estimate can estimate such sums well, and in the end all
   elements turn into constants and are reduced trivially.  Therefore
   we combine such sums into EXPR_ELSUM expressions, which are treated
   as a whole by reduction procedures.
*/

enum expr_tag {
  EXPR_CONST, EXPR_NAME,
  EXPR_NEG, EXPR_NOT, EXPR_COMPL,
  EXPR_MINUS, EXPR_PLUS,
  EXPR_LSH, EXPR_RSH,
  EXPR_MUL, EXPR_DIV, EXPR_REM,
  EXPR_AND, EXPR_XOR, EXPR_OR,
  EXPR_LT, EXPR_LE, EXPR_GT, EXPR_GE, EXPR_EQ, EXPR_NE,
  EXPR_ANDAND, EXPR_OROR,
  EXPR_QUEST, EXPR_COLON,
  EXPR_RANGE,
  EXPR_ELEMENT,
  EXPR_ELSUM,
  EXPR_TAG_COUNT	/* number of elements in expr_tag enumeration */
};

/* Convert tag to bitmask for easy tag-in-set operations */

#define EXPR_TAG_MASK(TAG)  (1L<<(TAG))

#define EXPR_ISLEAF(E)  (EXPR_TAG_ISLEAF ((E)->tag))
#define EXPR_TAG_ISLEAF(TAG)  ((TAG) == EXPR_CONST		\
			       || (TAG) == EXPR_NAME		\
			       || (TAG) == EXPR_RANGE	\
			       || (TAG) == EXPR_ELEMENT		\
			       || (TAG) == EXPR_ELSUM)

#define EXPR_ISUNARY(E)  (EXPR_TAG_ISUNARY ((E)->tag))
#define EXPR_TAG_ISUNARY(TAG)  ((TAG) == EXPR_NEG		\
			      || (TAG) == EXPR_NOT 		\
			      || (TAG) == EXPR_COMPL)

#define EXPR_ISCONST(E)  (EXPR_TAG_ISCONST ((E)->tag))
#define EXPR_TAG_ISCONST(TAG)  ((TAG) == EXPR_CONST)

#define EXPR_ISCOMM(E)  (EXPR_TAG_ISCOMM ((E)->tag))
#define EXPR_TAG_ISCOMM(TAG) /* commutative */ \
  (EXPR_TAG_MASK (TAG) & (EXPR_TAG_MASK (EXPR_PLUS)		\
			  | EXPR_TAG_MASK (EXPR_MUL)		\
			  | EXPR_TAG_MASK (EXPR_AND)		\
			  | EXPR_TAG_MASK (EXPR_XOR)		\
			  | EXPR_TAG_MASK (EXPR_OR)		\
			  | EXPR_TAG_MASK (EXPR_ANDAND)		\
			  | EXPR_TAG_MASK (EXPR_OROR)))

/* shortcut: expr_free is called all too often just to decrease nrefs;
   save the cost of the procedure call in this case  */
#define EXPR_FREE(E) \
  do {				\
    if ((E)->nrefs == 1)	\
	expr_free (E);		\
    else			\
	(E)->nrefs--;		\
  } while (0)			\


typedef struct expr *expr;
struct expr {

#ifdef EXPR_DEBUG
    expr forw;		
    expr back;
    unsigned refcount;
#endif

    enum expr_tag tag;		/* what kind of expression it is */
    unsigned nrefs;		/* count of immediate refs to this node */
    unsigned reent:1;		/* used for catching cycles */
    unsigned reent2:1;		/* used for catching cycles by expr_print */

    union {
	expr_const_t constval;	/* for tag == EXPR_CONST */
	struct name *name;	/* for tag == EXPR_NAME */
	struct element *element; /* for tag == EXPR_ELEMENT */

	struct {		/* for operator tags: */
	    struct expr *l;	/* ptr to lhs operand (unused for unary) */
	    struct expr *r;	/* ptr to rhs operand */
	} ptr;

	struct {		/* for tag == EXPR_RANGE */
	    expr_const_t lo;	/* lower bound */
	    expr_const_t hi;	/* higher bound */
	} range;

	struct {		/* for EXPR_ELSUM */
	    expr *vector;	/* vector of EXPR_ELEMENTs */
	    int count;		/* number of pointers in the vector */
	} elsum;
    } u;
};

/* Flag telling expr_print to prepend memory address to ambiguous
   terms (ranges and nonames) or (if >= 2) even to every expression
   printed . */

extern int expr_print_long;

/* Debugging: list of all expression nodes in existance */

extern expr expr_list;


#ifndef NO_LIBC

expr expr_alloc (enum expr_tag);
void expr_free (expr e);
expr expr_share (expr e);
expr expr_unshare (expr e);
expr expr_node_copy (expr to, expr from);
int expr_equal (expr e, expr f);
expr expr_const (expr_const_t value);
expr expr_name (struct name *np);
expr expr_range (expr_const_t lo, expr_const_t hi);
expr expr_element (struct element *el);
expr expr_elsum (void);
void expr_elsum_append (expr e, expr eel);
expr expr_unop (enum expr_tag op, expr e1);
expr expr_binop (enum expr_tag op, expr e1, expr e2);
expr expr_unop_owned (enum expr_tag op, expr e1);
expr expr_binop_owned (enum expr_tag op, expr e1, expr e2);
expr expr_questop (expr cond, expr iftrue, expr ifnot);
void expr_asgop (enum expr_tag op, expr *e1, expr e2);	/* +=, etc */
void expr_plus_constant (expr *e1, expr_const_t);
void expr_plus_element (expr *ep, struct element *el);
int expr_reduce (expr e);
int expr_check_alignment (expr e, int align);
expr expr_estimate (expr);
void expr_print (FILE *f, expr e);
int expr_name_mentioned (expr e, struct name *np);
void expr_count_refs (expr e);
void *name_count_refs (struct name *np);
void expr_statistics (char *header);
#endif NO_LIBC

#endif /* not EXPR_H */
