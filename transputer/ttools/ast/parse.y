/* $Id: parse.y,v 1.1.1.1 1995/12/22 12:24:38 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

%token	<u.name>	TOK_NAME	/* identifier */
%token  <u.str>		TOK_STRING	/* string constant */
%token  <u.lnum>	TOK_NUMBER	/* number is a number is a number */
%token  <u.num>		TOK_DIGLAB	/* digital label */
%token  <u.num>		TOK_DIGREF_B	/* digital label back reference */
%token  <u.num>		TOK_DIGREF_F	/* digital label forward reference */
%token  <u.num>		TOK_CMD_DIRECT	/* direct command */
%token  <u.num>		TOK_CMD_DIRECT_PCREL /* direct cmd w/pc-relative arg */
%token  <u.num>		TOK_CMD_INDIR	/* indirect command */
%token  <u.num>		TOK_CMD_FPU	/* doubly-indirect t8 fpu-family cmd */

/* directives */

%token			TOK_BYTE	/* .byte */
%token			TOK_HALF	/* .half */
%token			TOK_WORD	/* .word */
%token			TOK_ALIGN	/* .align */
%token			TOK_GLOBL	/* .globl */
%token			TOK_COMM	/* .comm */
%token			TOK_NEED	/* .need */
%token			TOK_SLOT	/* .slot */
%token			TOK_STRING_DIR	/* .string (null-terminated) */
%token			TOK_DCOLON	/* :: */
%token	<u.name>	TOK_SEGMENT	/* .text/.data/.bss/... */

/* nonterminals that return a value */

%type	<u.expr>	maybe_filler
%type	<u.expr>	expr

/* operators */

%right		'?' ':'
%left		TOK_OROR
%left		TOK_ANDAND
%left		'|'
%left		'^'
%left		'&'
%left		TOK_EQ TOK_NE
%left		'<' '>' TOK_LE TOK_GE
%left		TOK_LSH TOK_RSH
%left		'+' '-'
%left		'*' '/' '%'
%left		TOP	/* highest precedence */

%{
#include "ast.h"
#include "y.tab.h"

/* The segment we are currently emitting to.  */
struct segment *curseg;

/* Nth element holds the expression for the last digital label N seen. */
static expr backlab[10];

/* Every time a forward reference is seen, we create an undefined name
   and put the pointer to that here.  When we encounter a diglab,
   we assign the current point to the corresponding name and zero
   the entry in this array.  */
static name forwlab[10];

/* Used in handling of .byte/.half/.word */
static int data_width;

/* Vector of stored .need directives to be handled at end of parsing */
struct deferred_needs {
    name name;
    name dep;
};
static struct deferred_needs *deferred_needs = NULL;
static int ndeferred_needs = 0;


static void do_pending_alignment (struct segment *seg);
static void drop_pending_alignment (struct segment *seg);
static void *check_defined (name np);
static void yyerror (char *errstr);

%}

%%
source
	: initialize statements {
	    /*
	     * Tie ends at the end of parsing
	     */
	    int i;

	    /* line number would not be meaningful in the error()s
               below; turn it off */
	    error_report->line_number_ptr = NULL;

	    /* check whether all forward refs to digital labels have
               met their labels */
	    for (i=0; i <= 9; i++) {
		if (forwlab[i] != NULL) {
		    error ("hanging forward reference to digital label %d\n");
		    forwlab[i]->defined = 1;  /* do not catch it again below */
		}
	    }

	    /* complain if there are undefined name references */
	    table_walk (&general_names, check_defined);

	    /* Add deferred needs now that all names are defined */
	    for (i=0; i < ndeferred_needs; i++) {
		expr e = expr_name (deferred_needs[i].dep);

		expr_reduce (e);
		record_needs_name = deferred_needs[i].name;
		record_needs (e);
		EXPR_FREE (e);
	    }
	}
	;

initialize
	: /* empty */ {
	    /*
	     * Initialization of data used in actions
	     */

	    /* We make default fragments noname, because default
	       fragments in different segments/files should not be
	       unified, and noname symbols provide exactly this
	       semantics. */
	    name deffrag = table_add_noname (&general_names);

	    /* Unlike default fragments, default segment is not
               noname: instances of the default segment in different
               files should be merged into a single segment during
               link phase. */
	    name segname = table_lookup (&segment_names, DEFAULT_SEGMENT_IDENT,
					 sizeof (DEFAULT_SEGMENT_IDENT)-1);

	    globalize_name (deffrag);
	    deffrag->fragment = 1;
	    deffrag->defined = 1;
	    curseg = add_segment (segname, deffrag);

	    memset (forwlab, 0, sizeof (forwlab));
	    memset (backlab, 0, sizeof (backlab));
	}

statements
	: /* empty */
	| statements statement {
	    if (traceset & TRACE_POINT) {
		printf ("** POINT ");
		expr_print (stdout, curseg->point);
		printf ("\n");
	    }
	    if (traceset & TRACE_EXPR)
		expr_fsck ();
	}
	;

statement
	: label
	| command
	| directive
	| assignment
	  /* semicolons are considered empty statements, so you can
	     customarily separate command with them, if you just
	     cannot help doing so unconsciously.  This should help
	     error recovery, too.  */
	| ';'
	| error
	;

label
	: TOK_NAME ':' {
	    if ($1->defined)
		error2 ($<file>1, $<line>1,
			"'%s' already defined\n", $1->ident);
	    $1->defined = 1;
	    if ($1->global) {
		/* public label -- start new fragment */
		$1->fragment = 1;
		$1->align = curseg->pending_alignment;
		drop_pending_alignment (curseg);
		add_fragment (curseg, $1);
	    }
	    else {
		$1->align = curseg->pending_alignment;
		if (curseg->pending_alignment > 1)
		    do_pending_alignment (curseg);
		/* local label; assign it the current point value */
		name_assign_value ($1, curseg->point);
	    }
	}

	| TOK_NAME TOK_DCOLON {
	    if ($1->defined)
		error2 ($<file>1, $<line>1,
			"'%s' already defined\n", $1->ident);
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    $1->defined = 1;
	    name_assign_value ($1, curseg->point);
	}

	| TOK_DIGLAB ':' {	
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    if (backlab[$1])
		EXPR_FREE (backlab[$1]);
	    backlab[$1] = expr_share (curseg->point);
	    if (forwlab[$1] != NULL) {
		name_assign_value (forwlab[$1], curseg->point);
		forwlab[$1]->defined = 1;
		forwlab[$1] = NULL;
	    }
	}
	;

command
	: TOK_CMD_DIRECT expr {
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_cmd (curseg, $1, $2);
	    EXPR_FREE ($2);
	}

	| TOK_CMD_DIRECT_PCREL expr {
	    name np = table_add_noname (&general_names);
	    expr temp;

	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);

	    temp = expr_binop_owned (EXPR_MINUS, $2, expr_name (np));
	    emit_cmd (curseg, $1, temp);
	    EXPR_FREE (temp);

	    name_assign_value (np, curseg->point);
	    np->defined = 1;
	}

	| TOK_CMD_INDIR {
	    expr temp;
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_cmd (curseg, OPCODE_OPR, temp = expr_const ($1));
	    EXPR_FREE (temp);
	}

	| TOK_CMD_FPU {
	    expr temp;
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_cmd (curseg, OPCODE_LDC, temp = expr_const ($1));
	    EXPR_FREE (temp);
	    emit_cmd (curseg, OPCODE_OPR, temp = expr_const (OPCODE_FPENTRY));
	    EXPR_FREE (temp);
	}
	;

directive
	: TOK_SEGMENT {
	    if ($1->val)
		curseg = (segment) $1->val;
	    else {
		name deffrag = table_add_noname (&general_names);

		globalize_name (deffrag);
		deffrag->fragment = 1;
		deffrag->defined = 1;

		curseg = add_segment ($1, deffrag);

		/* For .bss, issue the second default fragment.  See
                   the comment on the .comm handling code. */
		if (strcmp ($1->ident, "bss") == 0) {
		    deffrag = table_add_noname (&general_names);
		    globalize_name (deffrag);
		    deffrag->fragment = 1;
		    deffrag->defined = 1;
		    add_fragment (curseg, deffrag);
		}
	    }
	}

	| TOK_GLOBL TOK_NAME {
	    if (! $2->global && $2->defined)
		error2 ($<file>2, $<line>2,
			"'%s' already used as non-global\n", $2->ident);
	    globalize_name ($2);
	}

        /* .comm creates a fragment of the said size in the .bss
	   segment.  A little diifculty with it is that we can't put
	   this fragment at the end of bss, since doing so would leave
	   BSS without current fragment -- we cannot emit anything
	   else in the .comm fragment!  Well, no problem -- we add the
	   .comm fragment at the end of .bss, and then swap two last
	   fragments.  However, this might put the .comm fragment
	   ahead of the default fragment, which would complicate
	   things in the linker; to prevent this, we issue two
	   default fragments instead of one for .bss */

	| TOK_COMM TOK_NAME ',' expr {
	    expr temp;
	    segment segbss;
	    name segname = table_lookup (&segment_names, "bss",
					 sizeof ("bss")-1);
	    if (segname->val)
		segbss = (segment) segname->val;
	    else {
		name deffrag = table_add_noname (&general_names);

		globalize_name (deffrag);
		deffrag->fragment = 1;
		deffrag->defined = 1;

		segbss = add_segment (segname, deffrag);

		/* add 2nd default fragment, .bss-specific */
		deffrag = table_add_noname (&general_names);
		globalize_name (deffrag);
		deffrag->fragment = 1;
		deffrag->defined = 1;
		add_fragment (segbss, deffrag);
	    }

	    if ($2->defined)
		error2 ($<file>2, $<line>2,
			"'%s' already defined\n", $2->ident);
	    globalize_name ($2);
	    $2->fragment = 1;
	    $2->defined = 1;
	    $2->common = 1;
	    $2->align = curseg->pending_alignment;  /* curseg, not segbss. */
	    drop_pending_alignment (curseg);

	    add_fragment (segbss, $2);
	    emit_data (segbss, 1, temp = expr_const (0), $4);
	    EXPR_FREE (temp);
	    swap_last_fragments (segbss);
	    EXPR_FREE ($4);
	}

	| TOK_NEED TOK_NAME {
	    if ($2->global)
		add_need (curseg->fragments[curseg->nfragments-1].name, $2);
	    else {
		/* For a nonglobal name, we should add needs for all
                   global names this one is defined through.  However,
                   we cannot do this right away, since the name might
                   not be defined yet.  So we defer actual need-adding
                   until the end of parsing */
		if (VECTOR_FULL (64, ndeferred_needs))
		    VECTOR_GROW (64, ndeferred_needs,
				 struct deferred_needs, deferred_needs);
		deferred_needs[ndeferred_needs].name
		    = curseg->fragments[curseg->nfragments-1].name;
		deferred_needs[ndeferred_needs].dep = $2;
		ndeferred_needs++;
	    }
	}

	| TOK_SLOT expr {
	    if (! EXPR_ISCONST ($2)) {
		error2 ($<file>2, $<line>2,
			"%s should be a constant expression\n",
			"the argument for .slot");
		EXPR_FREE ($2);
		break;
	    }
	    curseg->slot = $2->u.constval;
	    EXPR_FREE ($2);
	}

	| TOK_BYTE {data_width = 1;} datalist

	| TOK_STRING_DIR TOK_STRING {
	    /* .string "..." — emit string data plus null terminator */
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_block (curseg, $2.ptr, $2.len);
	    emit_block (curseg, "\0", 1);
	}

	| TOK_HALF {
	    data_width = 2;
	    if (curseg->pending_alignment < 2
		&& expr_check_alignment (curseg->point, 2) >= 2)
		warning2 ($<file>1, $<line>1,
			"data don't seem to be aligned properly\n");
	  } datalist

	| TOK_WORD {
	    data_width = 4;
	    if (curseg->pending_alignment < 4
		&& expr_check_alignment (curseg->point, 4) >= 4)
		warning2 ($<file>1, $<line>1,
			"data don't seem to be aligned properly\n");
	  } datalist

	| TOK_ALIGN expr maybe_filler {
	    if (! EXPR_ISCONST ($2)) {
		error2 ($<file>2, $<line>2,
			"%s should be a constant expression\n",
			"alignment factor");
		EXPR_FREE ($2);
		EXPR_FREE ($3);
		break;
	    }
	    if ($2->u.constval < 1) {
		warning2 ($<file>2, $<line>2,
			  "alignment of %d is meaningless\n", $2->u.constval);
		break;
	    }
	    /* Semantics of two succesive .align's is that the first
               one is emitted in the current fragment, and the second
               one becomes the pending alignment. */
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    curseg->pending_alignment = $2->u.constval;
	    curseg->pending_alignment_filler = $3;
	    EXPR_FREE ($2);
	}
	;

maybe_filler
	: /* empty -- use default filler */ {
	    $$ = expr_const (strcmp (curseg->name->ident, "text") == 0
			     ? OPCODE_PFIX
			     : 0);
	}
	| ',' expr {
	    $$ = $2;
	}
	;

datalist
	: datum
	| datalist ',' datum
	;
datum
	: expr {
	    expr temp;
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_data (curseg, data_width, $1, temp = expr_const (1));
	    EXPR_FREE (temp);
	    EXPR_FREE ($1);
	}

	| '?' {
	    expr count = expr_const (1);
	    expr data = expr_const (0);
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_data (curseg, data_width, data, count);
	    EXPR_FREE (data);
	    EXPR_FREE (count);
	}

	| expr '[' expr ']' {
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_data (curseg, data_width, $1, $3);
	    EXPR_FREE ($1);
	    EXPR_FREE ($3);
	}

	| '?' '[' expr ']' {
	    expr data = expr_const (0);
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_data (curseg, data_width, data, $3);
	    EXPR_FREE (data);
	    EXPR_FREE ($3);
	}

	| TOK_STRING {
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    if (data_width != 1) {
		error2 ($<file>1, $<line>1,
		  "strings are only allowed with .byte\n");
		break;
	    }
	    emit_block (curseg, $1.ptr, $1.len);
	}
	;

assignment
	: TOK_NAME '=' expr {
	    if ($1->defined) {
		error2 ($<file>1, $<line>1,
			"'%s' already defined\n", $1->ident);
		EXPR_FREE ($3);
		break;
	    }
	    $1->defined = 1;
	    name_assign_value ($1, $3);
	    EXPR_FREE ($3);
	}
	;

expr
	: TOK_NUMBER {
	    $$ = expr_const ($1);
	}

	| TOK_NAME {
	    $$ = expr_name ($1);
	}

	| '.' {
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    $$ = expr_share (curseg->point);
	}

	| TOK_DIGREF_B {
	    $$ = expr_share (backlab[$1]);
	}

	| TOK_DIGREF_F {
	    if (forwlab[$1] == NULL) {
		forwlab[$1] = table_add_noname (&general_names);
	    }
	    $$ = expr_name (forwlab[$1]);
	}

	| '(' expr ')'		{ $$ = $2; }
	| expr '+' expr		{ $$ = expr_binop_owned (EXPR_PLUS, $1, $3); }
	| expr '-' expr		{ $$ = expr_binop_owned (EXPR_MINUS, $1, $3); }
	| expr '&' expr 	{ $$ = expr_binop_owned (EXPR_AND, $1, $3); }
	| expr '^' expr 	{ $$ = expr_binop_owned (EXPR_XOR, $1, $3); }
	| expr '|' expr 	{ $$ = expr_binop_owned (EXPR_OR, $1, $3); }
	| expr '*' expr 	{ $$ = expr_binop_owned (EXPR_MUL, $1, $3); }
	| expr '/' expr 	{ $$ = expr_binop_owned (EXPR_DIV, $1, $3); }
	| expr '%' expr 	{ $$ = expr_binop_owned (EXPR_REM, $1, $3); }
	| expr '<' expr 	{ $$ = expr_binop_owned (EXPR_LT, $1, $3); }
	| expr '>' expr 	{ $$ = expr_binop_owned (EXPR_GT, $1, $3); }
	| expr TOK_LE expr	{ $$ = expr_binop_owned (EXPR_LE, $1, $3); }
	| expr TOK_GE expr      { $$ = expr_binop_owned (EXPR_GE, $1, $3); }
	| expr TOK_EQ expr      { $$ = expr_binop_owned (EXPR_EQ, $1, $3); }
	| expr TOK_NE expr      { $$ = expr_binop_owned (EXPR_NE, $1, $3); }
	| expr TOK_LSH expr     { $$ = expr_binop_owned (EXPR_LSH, $1, $3); }
	| expr TOK_RSH expr     { $$ = expr_binop_owned (EXPR_RSH, $1, $3); }
	| expr TOK_OROR expr	{ $$ = expr_binop_owned (EXPR_OROR, $1, $3); }
	| expr TOK_ANDAND expr	{ $$ = expr_binop_owned (EXPR_ANDAND, $1, $3); }
	| '+' expr %prec TOP	{ $$ = $2; }
	| '-' expr %prec TOP	{ $$ = expr_unop_owned (EXPR_NEG, $2); }
	| '!' expr %prec TOP	{ $$ = expr_unop_owned (EXPR_NOT, $2); }
	| '~' expr %prec TOP	{ $$ = expr_unop_owned (EXPR_COMPL, $2); }
	| expr '?' expr ':' expr {
	    $$ = expr_questop ($1, $3, $5);
	    EXPR_FREE ($1);
	    EXPR_FREE ($3);
	    EXPR_FREE ($5);
	}
	;
%%

static void
yyerror (char *errstr)
{
       error ("%s\n", errstr);
}

static void
do_pending_alignment (struct segment *seg)
{

    if (emit_align (seg, seg->pending_alignment, seg->pending_alignment_filler)) {
	/* emit_align has actually emitted something; let's make the
           point look aligned for expr_check_alignment to be able to
           do its job  */
	name np = table_add_noname (&general_names);

	name_assign_value (np, seg->point);
	np->align = seg->pending_alignment;
	np->defined = 1;
	EXPR_FREE (seg->point);
	seg->point = expr_name (np);
    }

    drop_pending_alignment (seg);
}

static void
drop_pending_alignment (struct segment *seg)
{
    seg->pending_alignment = 1;
    if (seg->pending_alignment_filler) {
	EXPR_FREE (seg->pending_alignment_filler);
	seg->pending_alignment_filler = NULL;
    }
}

/* Issue an error message if name is not defined.  Used over the whole
   namespace once the source parsing is complete. */

static void *
check_defined (name np)
{
    if (!np->defined && !np->global)
	error ("`%s' undefined\n", np->ident);
    return NULL;
}

/* Print a token in readable form for debugging purposes */

void
print_token (FILE *stream, int token, YYSTYPE *tp)
{
    fprintf (stream, "** LEX %s:%d ", tp->file, tp->line);
    switch (token) {
    case TOK_NAME:
	fprintf (stream, "NAME %s\n", tp->u.name->ident);
	return;
    case TOK_STRING:
	{
	    int i;
	    fprintf (stream, "STRING \"");
	    for (i=0; i < tp->u.str.len; i++)
		fprintf (stream, isprint (tp->u.str.ptr[i])? "%c": "\\%03o",
			 tp->u.str.ptr[i]);
	    fprintf (stream, "\"\n");
	    return;
	}
    case TOK_NUMBER:
	fprintf (stream, "NUMBER %ld (0x%lx)\n", tp->u.lnum, tp->u.lnum);
	return;
    case TOK_DIGLAB:
	fprintf (stream, "DIGLAB %d\n", tp->u.num);
	return;
    case TOK_DIGREF_B:
	fprintf (stream, "DIGREF_B %d\n", tp->u.num);
	return;
    case TOK_DIGREF_F:
	fprintf (stream, "DIGREF_F %d\n", tp->u.num);
	return;
    case TOK_CMD_DIRECT:
	fprintf (stream, "CMD_DIRECT 0x%x\n", tp->u.num);
	return;
    case TOK_CMD_DIRECT_PCREL:
	fprintf (stream, "CMD_DIRECT_PCREL 0x%x\n", tp->u.num);
	return;
    case TOK_CMD_INDIR:
	fprintf (stream, "CMD_INDIR 0x%x\n", tp->u.num);
	return;
    case TOK_CMD_FPU:
	fprintf (stream, "CMD_FPU 0x%x\n", tp->u.num);
	return;
    case TOK_BYTE:
	fprintf (stream, "BYTE\n");
	return;
    case TOK_HALF:
	fprintf (stream, "HALF\n");
	return;
    case TOK_WORD:
	fprintf (stream, "WORD\n");
	return;
    case TOK_ALIGN:
	fprintf (stream, "ALIGN\n");
	return;
    case TOK_GLOBL:
	fprintf (stream, "GLOBL\n");
	return;
    case TOK_NEED:
	fprintf (stream, "NEED\n");
	return;
    case TOK_SLOT:
	fprintf (stream, "SLOT\n");
	return;
    case TOK_DCOLON:
	fprintf (stream, "DCOLON\n");
	return;
    case TOK_SEGMENT:
	fprintf (stream, "SEGMENT %s\n", tp->u.name->ident);
	return;
    case TOK_OROR:
	fprintf (stream, "OROR\n");
	return;
    case TOK_ANDAND:
	fprintf (stream, "ANDAND\n");
	return;
    case TOK_EQ:
	fprintf (stream, "EQ\n");
	return;
    case TOK_NE:
	fprintf (stream, "NE\n");
	return;
    case TOK_LE:
	fprintf (stream, "LE\n");
	return;
    case TOK_GE:
	fprintf (stream, "GE\n");
	return;
    case TOK_LSH:
	fprintf (stream, "LSH\n");
	return;
    case TOK_RSH:
	fprintf (stream, "RSH\n");
	return;
    default:
	if (token >= ' ' && token < 0x7f)
	    fprintf (stream, "'%c'\n", token);
	else
	    abort ();
	return;
    }
}

void
expr_fsck ()
{
#ifdef EXPR_DEBUG
    expr e;
    int i;

    for (e = expr_list->forw; e != expr_list; e = e->forw)
	e->refcount = 0;

    pass_count_refs ();
    table_walk (&general_names, name_count_refs);
    for (i=0; i < nsegments; i++) {
	if (segments[i]->point)
	    expr_count_refs (segments[i]->point);
	if (segments[i]->pending_alignment_filler)
	    expr_count_refs (segments[i]->pending_alignment_filler);
    }
    for (i=0; i <= 9; i++) {
	if (backlab[i])
	    expr_count_refs (backlab[i]);
    }

    for (e = expr_list->forw; e != expr_list; e = e->forw) {
	if (e->refcount != e->nrefs) {
	    printf ("** EXPR %p nrefs %u -- counted %u\n",
		    e, e->nrefs, e->refcount);
	}
	else if (e->refcount == 0) {
	    printf ("** EXPR %p unreferenced\n", e);
	}
    }
#endif
}
