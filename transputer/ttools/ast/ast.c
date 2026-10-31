/* $Id: ast.c,v 1.1.1.1 1995/12/22 12:24:38 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#include "ast.h"
#include <errno.h>

static void output_nametable (void);
static void output_needs (void);
static void *output_needs1 (name);
static void output_definitions (void);
static void *output_definitions1 (name);
static void output_stabs (void);

char *infile_n = "stdin";	/* defaults to stdin if no args */
char *outfile_n = "a.out";	/* defaults to a.out unless -o specified */
FILE *outfile_f;	

/* Vector of global names built by parse.y.  Indexed by the object
   file indices. */

static name *global_names = NULL;
static int nglobal_names = 0;

static char *myname = "ast";

static char *option_list = 
#undef DEFINE_OPTION
#define DEFINE_OPTION(name, has_arg, flag, val, descr) descr
#include "options.h"
;

static struct option opttab[] = {
#undef DEFINE_OPTION
#define DEFINE_OPTION(name, has_arg, flag, val, x) {name, has_arg, flag, val},
#include "options.h"
};

static struct trace_tab trace_tab[] = {
    { "all", ~0 },
    { "lex", TRACE_LEX },
    { "point", TRACE_POINT },
    { "opt", TRACE_OPT },
    SHARED_TRACE_FLAGS
    { NULL, 0 }
};
static struct trace_tab dump_tab[] = {
    { "all", ~0 },
    { "parse", DUMP_PARSE },
    { "opt", DUMP_OPT },
    { "canon", DUMP_CANON },
    { "unroll", DUMP_UNROLL },
    { "merge", DUMP_MERGE },
    { "needs", DUMP_NEEDS },
    { NULL, 0 }
};

enum usage_flavor {
    USAGE_GENERAL,
    USAGE_OPTIONS,
};

void
usage (char *errstr, enum usage_flavor flavor, int exit_code)
{
    if (errstr)
	fprintf (stderr, "%s", errstr);

    switch (flavor) {
    case USAGE_GENERAL:
	fprintf (stderr, "This is ast version %s\n", version_string);
	fprintf (stderr, "Usage: %s [options] [FILE]\n", myname);
	fprintf (stderr, "%s --help for options list\n", myname);
	break;

    case USAGE_OPTIONS:
	fprintf (stderr, "This is ast version %s\n", version_string);
	fprintf (stderr, "Usage: %s [options] [FILE]\n", myname);
	fprintf (stderr, "If no FILE given, assemble from stdin. Options are:\n");
	fprintf (stderr, "%s", option_list);
	fprintf (stderr, "\n");
	fprintf (stderr, "--trace and --dump specs are of the form KEY[+KEY][-KEY]...\n");
	fprintf (stderr, "  where +KEY enables certain facility, -KEY disables it.\n");
	fprintf (stderr, "  Available trace keys: ");
	{
	    struct trace_tab *tp = trace_tab;
	    fprintf (stderr, "%s", tp->id);
	    while ((++tp)->id)
		fprintf (stderr, ",%s", tp->id);
	    fprintf (stderr, "\n");
	}
	fprintf (stderr, "  Available dump keys: ");
	{
	    struct trace_tab *tp = dump_tab;
	    fprintf (stderr, "%s", tp->id);
	    while ((++tp)->id)
		fprintf (stderr, ",%s", tp->id);
	    fprintf (stderr, "\n");
	}
	break;

    default:
	abort ();
    }

    exit (exit_code);
}


int
main (int argc, char **argv)
{
    int i, c;
    int maxpasses = 8;
    char *p;
    static int dummy = 0;

    /* Find out how we are named on this system */
    if ((myname = strrchr (argv[0], '/') + 1) == NULL + 1)
	myname = argv[0];

    /* Parse options */
    while ((c = getopt_long (argc, argv, "d:o:p:t:E",
			     opttab, &dummy)) != -1) {
	switch (c) {
	case '?':		/* an illegal option */
	    usage ("", USAGE_GENERAL, 1);

	case OPTION_HELP:
	    usage ("", USAGE_OPTIONS, 0);

	case 'd':
	    if (parse_trace_spec (optarg, dump_tab, &dumpset) < 0)
		usage ("invalid argument for --trace\n", USAGE_OPTIONS, 1);
	    break;

	case 'o':
	    outfile_n = optarg;
	    break;

	case 'p':
	    maxpasses = strtol (optarg, &p, 10);
	    if (*p != '\0')
		usage ("invalid argument for --maxpasses", USAGE_OPTIONS, 1);
	    break;

	case 't':
	    if (parse_trace_spec (optarg, trace_tab, &traceset) < 0)
		usage ("invalid argument for --trace\n", USAGE_OPTIONS, 1);
	    break;

	case 'E':
	    expr_print_long++;	/* 1 -- long, 2 -- looong */
	    break;

	default:
	    abort ();
	}
    }

    /* Now, what we're going to assemble? open the file if not stdin. */
    if (optind < argc) {
	infile_n = argv[optind++];
	if (strcmp (infile_n, "-") != 0)
	    if (! freopen (infile_n, "rt", stdin))
		fatal ("cannot open '%s': %s\n", infile_n, strerror (errno));
    }

    /* We are simple guys and so only accept a single input file */
    if (optind < argc)
	usage ("too many arguments\n", USAGE_GENERAL, 1);

    /* disable stdio buffering on stdout, which we use for debugging output */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* initialize error reporting */
    input_filename = infile_n;
    error_report->my_name = myname;
    error_report->file_name = &input_filename;
    error_report->line_number_ptr = &input_lineno;
    
    /* initialize namespaces */
    table_init (&file_names, 32);
    table_init (&segment_names, 32);
    table_init (&general_names, 1024);

    if (yyparse () != 0)
	/* our parser should go till EOF even if the input is all
           garbage. */
	abort ();

    if (*error_report->n_errors)
	exit (1);

    if (dumpset & DUMP_PARSE)
	for (i=0; i < nsegments; i++)
	    segment_dump (stdout, ">> PARSE ", segments[i]);
    if (traceset & TRACE_STAT)
	expr_statistics ("PARSE");
    if (traceset & TRACE_EXPR)
	expr_fsck ();


    /* Free point expressions in the segments; from now on, they would
       only unnesessarily occupy memory or hold extra references in
       expressions and elements. */
    for (i=0; i < nsegments; i++) {
	EXPR_FREE (segments[i]->point);
	segments[i]->point = NULL;
    }

    for (i=1; i <= maxpasses; i++) {
	int effect = pass_optimize_offsets ();
	if (traceset & TRACE_OPT)
	    printf ("** OPT pass %d made %d shrinks\n", i, effect);
	if (dumpset & DUMP_OPT) {
	    for (i=0; i < nsegments; i++)
		segment_dump (stdout, ">> OPT ", segments[i]);
	}
	if (traceset & TRACE_STAT)
	    expr_statistics ("OPT");
	if (traceset & TRACE_EXPR)
	    expr_fsck ();
	if (*error_report->n_errors)
	    exit (1);
	if (effect == 0)
	    break;		/* unshrinkable -- enough of it */
    }


    pass_canonicalize ();
    if (dumpset & DUMP_CANON)
	for (i=0; i < nsegments; i++)
	    segment_dump (stdout, ">> CANON ", segments[i]);
    if (traceset & TRACE_STAT)
	expr_statistics ("CANON");
    if (traceset & TRACE_EXPR)
	expr_fsck ();
    if (*error_report->n_errors)
	exit (1);


    pass_unroll ();
    if (dumpset & DUMP_UNROLL)
	for (i=0; i < nsegments; i++)
	    segment_dump (stdout, ">> UNROLL ", segments[i]);
    if (traceset & TRACE_STAT)
	expr_statistics ("UNROLL");
    if (traceset & TRACE_EXPR)
	expr_fsck ();
    if (*error_report->n_errors)
	exit (1);


    pass_merge ();
    if (dumpset & DUMP_MERGE)
	for (i=0; i < nsegments; i++)
	    segment_dump (stdout, ">> MERGE ", segments[i]);
    if (traceset & TRACE_STAT)
	expr_statistics ("MERGE");
    if (traceset & TRACE_EXPR)
	expr_fsck ();
    if (*error_report->n_errors)
	exit (1);


    pass_needs ();
    if (*error_report->n_errors)
	exit (1);


    /* And in the end, write the output file */

    if (! (outfile_f = fopen (outfile_n, "wb")))
	fatal ("%s: %s", outfile_n, strerror (errno));

    lit_out_file = outfile_f;
    lit_out_file_name = outfile_n;

    output_nametable ();
    output_needs ();
    pass_output_segments ();
    output_definitions ();
    output_stabs ();

    if (ferror (outfile_f)) {
	fatal ("%s: %s\n", outfile_n, strerror (errno));
	unlink (outfile_n);
    }

    return 0;
}

void
output_nametable ()
{
    int i;
    for (i=0; i < nglobal_names; i++)
	lit_out_ntab_entry (global_names[i]);
    lit_out_marker (LIT_AREA_NAMETABLE);
}

void
output_needs ()
{
    table_walk (&general_names, output_needs1);
    lit_out_marker (LIT_AREA_NEEDS);
}

void *
output_needs1 (name np)
{
    if (np->global && np->defined) {

	/* Non-fragment names do not have needs list formed yet; form
           it now */
	if (! np->fragment) {
	    assert (np->val != NULL);
	    expr_reduce (np->val);
	    record_needs_name = np;
	    record_needs (np->val);
	}

	if (np->needs && *np->needs) {
	    name *npp;

	    lit_out_number (LIT_NEEDS);
	    lit_out_number (np->index);
	    for (npp = np->needs; *npp; npp++)
		lit_out_number ((*npp)->index);
	    lit_out_number (-1);	/* End-of-list sign */
	}
    }
    return NULL;
}

void
output_definitions ()
{
    table_walk (&general_names, output_definitions1);
    lit_out_marker (LIT_AREA_DEFINITIONS);
}

void *
output_definitions1 (name np)
{
    if (np->global && np->defined && !np->fragment) {
	assert (np->val != NULL);
	lit_out_number (LIT_NAMEDEF);
	lit_out_number (np->index);
	lit_out_expr (np->val);
    }
    return NULL;
}

void
output_stabs ()
{
    /* no stabs yet, sorry */
}

void
globalize_name (name np)
{
    if (! np->global) {
	np->global = 1;
	np->index = nglobal_names;

	if (VECTOR_FULL (64, nglobal_names))
	    VECTOR_GROW (64, nglobal_names, name, global_names);
	global_names[nglobal_names++] = np;
    }
}
