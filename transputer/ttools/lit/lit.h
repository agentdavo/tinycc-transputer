/* $Id: lit.h,v 1.1.1.1 1995/12/22 12:24:39 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#include <unistd.h>
#include <ctype.h>
#include <ar.h>
#include "shared.h"

/* lit-specific trace flags; complement shared ones */
enum trace_mask_ast {
    TRACE_OPT	= 0x00000004,	/* report totals for every optimization pass */
    TRACE_FILES	= 0x00000008,	/* opening input files */
    TRACE_READ	= 0x00000010,	/* elements read and skipped */
    TRACE_NEEDS	= 0x00000020,	/* name dependencies, wanted marks */
    TRACE_POINT	= 0x00000040,	/* show point growing */
};

/* ast-specific dump flags */
enum dump_mask_ast {
    DUMP_PARSE	= 0x00000001,
    DUMP_OPT	= 0x00000002,
    DUMP_REDUCE	= 0x00000004,
    DUMP_UNROLL	= 0x00000008,
    DUMP_MERGE	= 0x00000010,
    DUMP_FREEZE	= 0x00000020,
};
