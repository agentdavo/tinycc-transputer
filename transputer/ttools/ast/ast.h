/* $Id: ast.h,v 1.1.1.1 1995/12/22 12:24:38 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#include <ctype.h>
#include "shared.h"

typedef struct {
    char *file;			/* file the token has been read from */
    unsigned int line;		/* line number of the token in the file */
    union {
	struct {		/* string (not necessarily null-terminated): */
	    int len;		/* its length */
	    char *ptr;
	} str;
	int num;
	long lnum;
	name name;
	expr expr;
    } u;
} YYSTYPE;

/* ast-specific trace flags; complement shared ones */
enum trace_mask_ast {
    TRACE_LEX	= 0x00000001,	/* print tokens returned by the scaner */
    TRACE_POINT = 0x00000002,	/* print program counter after every statement */
    TRACE_OPT	= 0x00000004,	/* report totals for every optimization pass */
};

/* ast-specific dump flags */
enum dump_mask_ast {
    DUMP_PARSE	= 0x00000001,
    DUMP_OPT	= 0x00000002,
    DUMP_CANON	= 0x00000004,
    DUMP_UNROLL	= 0x00000008,
    DUMP_MERGE	= 0x00000010,
    DUMP_NEEDS	= 0x00000020,
};

/* Variables maintained by scan.l */

extern char *input_filename;
extern int input_lineno;

void print_token (FILE *stream, int token, YYSTYPE *tp);
void globalize_name (name np);

/* avoid warnings about those being non-prototyped */
int yyparse (void);
int yylex (void);
