/* $Id: lit.c,v 1.5 1997/03/29 21:54:13 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#include "lit.h"
#include <errno.h>


static void output_stabs ();
static void ifile_add (char *fname);
static void ifile_add_member (struct ifile *master, long lo, long hi, char *);
static FILE *ifile_fopen (char *fname, char *mode);
static int ifile_isarchive (struct ifile *);
static void ifile_open_archive (struct ifile *);
static void ifile_read_nametable (struct ifile *);
static void ifile_read_segments (struct ifile *);
static segment setup_segment (char *seg_ident, int seg_ident_len);
static void ifile_read_definitions (struct ifile *);
static void mark_wanted (name np);
static void add_source (name np, struct ifile *ifi);
struct ifile *first_source (name np);
static void add_ref (name np, struct ifile *ifi);
static void sources_print (FILE *stream, name np);
static void *report_undefined (name np);
static void *check_defined (name np);
static void *name_free_value (name np);
static void *print_map (name np);
static void expr_check_elements (void);



char *outfile_n = "a.out";	/* defaults to a.out unless -o specified */
FILE *outfile_f;	

char *mapfile_n = "stdout";
FILE *mapfile_f = NULL;		/* no map file by default */

/* main() sets this to the name we run under, extracted from argv[0] */
static char *myname = "lit";

/* We assume the segments will be loaded at this boundary */
static int segment_align = 4;

struct ifile {
    char *fname;		/* pathname of the file */
    char *error_report_fname;	/* for library members, includes member name */
    FILE *stream_proper;	/* open file, or NULL, if closed temporarily */
    FILE **stream;		/* pointer to stream_proper in this or the
				   archive master ifile */
    long lo;			/* start offset; non-0 for archive members */
    long hi;			/* end offset; ==lo+file_length */
    long pos;			/* position saved after a pass */
    name *names;		/* index-to-name mapping table */
    int nnames;			/* number of entries in `names' */
    expr *elements;		/* index-to-element mapping table */
    int elements_allocated;	/* malloced size of `elements' */
    unsigned wanted:1;		/* flag: contains wanted fragments */
    unsigned archive:1;		/* flag: is a library file */
};

struct ifile **ifiles = NULL;
int nifiles = 0;

/* Ifile currently being read; used by name_by_index and
   element_by_index */
struct ifile *current_ifile;

/* The number of wanted names we have not yet seen definitions for.  We
   start with only @@hook or command-line hooks counted here, and go
   through the list of object files and libraries, decreasing the counter
   as we see definitions of wanted names and increasing it as we discover
   more needs. */
static int undefined_wanted_count = 0;

/* set by ifile_read_segments to indicate that current fragment is
   being skipped.  Also checked by element_by_index. */
static int skipping;

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
    { "opt", TRACE_OPT },
    { "opt2", TRACE_OPT2 },
    { "reduce", TRACE_REDUCE },
    { "files", TRACE_FILES },
    { "read", TRACE_READ },
    { "needs", TRACE_NEEDS },
    { "point", TRACE_POINT },
    { "misc", TRACE_MISC },
    { "expr", TRACE_EXPR },
    { NULL, 0 }
};
static struct trace_tab dump_tab[] = {
    { "all", ~0 },
    { "parse", DUMP_PARSE },
    { "opt", DUMP_OPT },
    { "reduce", DUMP_REDUCE },
    { "freeze", DUMP_FREEZE },
    { "unroll", DUMP_UNROLL },
    { "merge", DUMP_MERGE },
    { NULL, 0 }
};

enum usage_flavor {
    USAGE_GENERAL,
    USAGE_OPTIONS,
};

static void
usage (char *errstr, enum usage_flavor flavor, int exit_code)
{
    if (errstr)
	fprintf (stderr, "%s", errstr);

    switch (flavor) {
    case USAGE_GENERAL:
	fprintf (stderr, "This is lit version %s\n", version_string);
	fprintf (stderr, "Usage: %s [options] [OBJFILE ...]\n", myname);
	fprintf (stderr, "%s --help for options list\n", myname);
	break;

    case USAGE_OPTIONS:
	fprintf (stderr, "This is lit version %s\n", version_string);
	fprintf (stderr, "Usage: %s [options] [OBJFILE ...]\n", myname);
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
    int i, j, c;
    char *p;
    name np;
    int maxpasses = 8;
    int hooks_defined = 0;
    static int dummy = 0;

    char **libraries = NULL;
    int nlibraries = 0;

    char **libdir = NULL;
    int nlibdir = 0;
    
    char **defsym_name = NULL;
    expr *defsym_expr = NULL;
    int ndefsym = 0;

    /* initialize namespaces */
    table_init (&segment_names, 16);
    table_init (&general_names, 4096);

    /* Find out how we are named on this system */
    if ((myname = strrchr (argv[0], '/') + 1) == NULL + 1)
	myname = argv[0];
    error_report->my_name = myname;

    /* disable stdio buffering on stdout, which we use for debugging output */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Parse options */
    while ((c = getopt_long (argc, argv, "d:o:p:t:Ea:h:l:m::L:T::",
			     opttab, &dummy)) != -1) {
	switch (c) {
	case '?':		/* an illegal option */
	    usage ("", USAGE_GENERAL, 1);

	case OPTION_HELP:
	    usage ("", USAGE_OPTIONS, 0);

	case 'd':
	    if (parse_trace_spec (optarg, dump_tab, &dumpset) < 0)
		usage (xstrcat4 ("invalid argument for --dump: `",
				 optarg, "'\n", ""),
		       USAGE_OPTIONS, 1);
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
		usage (xstrcat4 ("invalid argument for --trace: `",
				 optarg, "'\n", ""),
		       USAGE_OPTIONS, 1);
	    break;

	case 'E':
	    expr_print_long++;	/* 1 -- long, 2 -- looong */
	    break;

        /* lit-specific options */

	case 'a':
	    segment_align = strtol (optarg, &p, 10);
	    if (*p != '\0')
		usage ("invalid argument for --segment-align", USAGE_OPTIONS, 1);

	case 'h':
	    np = table_lookup (&general_names, optarg, strlen (optarg));
	    mark_wanted (np);
	    hooks_defined++;
	    break;

	case 'l':
#ifdef noyet
	    /* A common practice with traditional linkers is to
	       mention the same library twice, so that it (a) takes
	       precedence over the libraries in between, and (b) those
	       libraries can reference symbols from it.  For example,
	       gcc usually invokes the linker with -lgcc -lc -lgcc.
	       lit does not need this trick because of its two-pass
	       scheme; so we can drop repetitions to avoid superfluous
	       scanning. */
	    for (i=0; i < nlibraries; i++)
		if (strcmp (libraries[i], optarg) == 0)
		    break;
	    if (i < nlibraries)
#endif
	    if (VECTOR_FULL (16, nlibraries))
		VECTOR_GROW (16, nlibraries, char *, libraries);
	    libraries[nlibraries++] = optarg;
	    break;

	case 'm':
  	    if (optarg) {
	        mapfile_n = optarg;
	        if (! (mapfile_f = fopen (mapfile_n, "wt")))
		    fatal ("%s: %s", mapfile_n, strerror (errno));
  	    }
	    else {
	        mapfile_f = stdout;
	    }
	    break;

	case 'L':
	    if (VECTOR_FULL (16, nlibdir))
		VECTOR_GROW (16, nlibdir, char *, libdir);
	    libdir[nlibdir++] = optarg;
	    break;

	case 'T':
	    /* We only record the assignment here, and create names
               after all options (notably -a) are parsed; this also
               allows to share code with --defsym */
	    if (optind >= argc) {
		usage (xstrcat4 ("missing argument for -T", optarg, "\n", ""),
		       USAGE_OPTIONS, 1);
	    }
	    if (VECTOR_FULL (8, ndefsym)) {
		VECTOR_GROW (8, ndefsym, char *, defsym_name);
		VECTOR_GROW (8, ndefsym, expr, defsym_expr);
	    }
	    defsym_name[ndefsym] = xstrcat4 (optarg? optarg: "text", "@@start", "", "");
	    defsym_expr[ndefsym] = expr_const (strtoul (argv[optind], &p, 0));
	    if (*p != '\0')
		usage (xstrcat4 ("invalid argument for -T", optarg, "\n", ""),
		       USAGE_OPTIONS, 1);
	    ndefsym++;
	    optind++;
	    break;

	default:
	    abort ();
	}
    }

    /* Add "standard places" to the library search path */

#define STRINGIFY(S)  STRINGIFY1(S)
#define STRINGIFY1(S)  #S

#ifdef LIBDIR
    if (VECTOR_FULL (16, nlibdir))
	VECTOR_GROW (16, nlibdir, char *, libdir);
    libdir[nlibdir++] = STRINGIFY (LIBDIR);
#endif /* LIBDIR */

#ifdef LLIBDIR
    if (VECTOR_FULL (16, nlibdir))
	VECTOR_GROW (16, nlibdir, char *, libdir);
    libdir[nlibdir++] = STRINGIFY (LLIBDIR);
#endif /* LLIBDIR */

    /* Now actually do symbol definitions */
    for (i=0; i < ndefsym; i++) {
	name np = table_lookup (&general_names, defsym_name[i],
				strlen (defsym_name[i]));
	if (np->special == NAME_SPECIAL_START)
	    np->align = segment_align;
	name_assign_value (np, defsym_expr[i]);
	EXPR_FREE (defsym_expr[i]);
	free (defsym_name[i]);
    }
    free (defsym_name);
    free (defsym_expr);
    ndefsym = 0;

    /* name_assign_value above might detect something incorrect, check */
    if (*error_report->n_errors)
	exit (1);

    if (optind >= argc)
	usage ("No input files given\n", USAGE_GENERAL, 1);

    /* If no hook specifications found on the command line, introduce the
       default hook */
    if (hooks_defined == 0) {
	np = table_lookup (&general_names, "@@hook", sizeof ("@@hook") - 1);
	mark_wanted (np);
    }

    /* First file pass: glance through the input files and libraries,
       building the dependency graph.  After this pass, we will know
       which files we want to look at in the second pass, and which
       fragments we want from these files. */

    /* First, explicit file arguments */

    for (i = optind; i < argc; i++) {
	int ifindex = nifiles;

	error_report->file_name = &argv[i];
	ifile_add (argv[i]);
	if (ifile_isarchive (ifiles[ifindex])) {
	    ifile_open_archive (ifiles[ifindex]);
	    ifindex++;
	}
	while (ifindex < nifiles) {
	    error_report->file_name = &ifiles[ifindex]->error_report_fname;
	    ifile_read_nametable (ifiles[ifindex]);
	    ifindex++;
	}
    }
    error_report->file_name = NULL;
    if (*error_report->n_errors)
	exit (1);

    /* Second, libraries specified with -lAR */

    for (i = 0; i < nlibraries; i++) {
	int ifindex = nifiles;
	char *fname;

	/* If all names are already resolved, don't scan the rest of
           libraries */
	if (undefined_wanted_count == 0)
	    break;

	assert (undefined_wanted_count > 0);

	for (j = 0; j < nlibdir; j++) {
	    fname = xstrcat4 (libdir[j], "/lib", libraries[i], ".a");
	    if (access (fname, R_OK) == 0)
		break;
	    free (fname);
	}
	if (j == nlibdir) {
	    warning ("cannot find library lib%s.a\n", libraries[i]);
	    continue;
	}

	error_report->file_name = &fname;
	ifile_add (fname);
	if (ifile_isarchive (ifiles[ifindex])) {
	    ifile_open_archive (ifiles[ifindex]);
	    ifindex++;
	}
	while (ifindex < nifiles) {
	    error_report->file_name = &ifiles[ifindex]->error_report_fname;
 	    ifile_read_nametable (ifiles[ifindex]);
	    ifindex++;
	}
	error_report->file_name = NULL;
    }
    if (*error_report->n_errors)
	exit (1);


    /* Have looked through all the input files.  If there are undefined
       symbols, report them and die */

    if (undefined_wanted_count != 0) {
	table_walk (&general_names, report_undefined);
	exit (1);
    }


    /* Second file pass: read in wanted fragments from the files we
       have chosen during the first pass.  */

    for (i=0; i < nifiles; i++) {
	if (ifiles[i]->wanted) {
	    error_report->file_name = &ifiles[i]->error_report_fname;
	    ifile_read_segments (ifiles[i]);
	}
    }
    error_report->file_name = NULL;
    if (traceset & TRACE_EXPR)
	expr_fsck ();
    if (*error_report->n_errors)
	exit (1);


    /* Assign values to <SEG>@@size's */

    for (i=0; i < nsegments; i++) {
	char *p = xstrcat4 (segments[i]->name->ident, "@@size", "", "");
	expr temp;
	expr uemp;

	if (traceset & TRACE_EXPR)
	    expr_fsck ();

	np = table_lookup (&general_names, p, strlen (p));
	free (p);

	/* we should have caught illegal assignments in
	   ifile_read_definitions */
	assert (! np->val);

	/* Make segment sizes a multiple of segment alignment; this
	   easies writing startup files with back-to-back segments. */
	if (segment_align > 1) {
	    expr temp;
	    emit_align (segments[i], segment_align, temp = expr_const (0));
	    EXPR_FREE (temp);
	}

	temp = expr_name (segments[i]->fragments[0].name);
	name_assign_value (np, uemp = expr_binop (EXPR_MINUS, segments[i]->point, temp));
	EXPR_FREE (temp);
	EXPR_FREE (uemp);

	/* We have made segment size a multiple of segment_align,
	   let's show it: might help optimzation... */
	np->align = segment_align;
    }
    if (dumpset & DUMP_PARSE)
	for (i=0; i < nsegments; i++)
	    segment_dump (stdout, ">> PARSE ", segments[i]);
    if (traceset & TRACE_EXPR)
	expr_fsck ();
    if (*error_report->n_errors)
	exit (1);

    /* Now that we have values for XX@@start's set up, we can safely
       read name definitions.  If we attempted it before, we could
       fail in name_assign_value on definitions of aligned names via
       XX@size, eg data@@start=text@@start+text@@size. */

    for (i=0; i < nifiles; i++) {
	if (ifiles[i]->wanted) {
	    error_report->file_name = &ifiles[i]->error_report_fname;
	    ifile_read_definitions (ifiles[i]);
	}
    }
    error_report->file_name = NULL;
    if (traceset & TRACE_EXPR)
	expr_fsck ();
    if (*error_report->n_errors)
	exit (1);

    /* Make sure all names claimed "defined" in the nametable area are
       actually defined. */

    table_walk (&general_names, check_defined);
    if (*error_report->n_errors)
	exit (1);


    /* Now make passes */

    for (i=1; i <= maxpasses; i++) {
	int effect = pass_optimize_offsets ();
	if (traceset & TRACE_OPT) {
	    printf ("** OPT pass %d made %d shrinks\n", i, effect);
	}
	if (effect == 0)
	    break;		/* unshrinkable -- enough of it */
    }
    if (dumpset & DUMP_OPT)
	for (i=0; i < nsegments; i++)
	    segment_dump (stdout, ">> OPT ", segments[i]);
    if (traceset & TRACE_EXPR)
	expr_fsck ();
    if (*error_report->n_errors)
	exit (1);


    pass_freeze ();
    if (dumpset & DUMP_FREEZE)
	for (i=0; i < nsegments; i++)
	    segment_dump (stdout, ">> FREEZE ", segments[i]);
    if (traceset & TRACE_EXPR)
	expr_fsck ();
    if (*error_report->n_errors)
	exit (1);


    /* If any of XX@@blocksize's are defined, setup per-segment block
       thresholds.  This must be done before we free name values below. */

    for (i=0; i < nsegments; i++) {
	char *p = xstrcat4 (segments[i]->name->ident, "@@blocksize", "", "");
	name np = table_lookup (&general_names, p, strlen (p));
	free (p);

	if (np->val) {
	    expr_reduce (np->val);
	    if (EXPR_ISCONST (np->val))
		segments[i]->block_threshold = np->val->u.constval;
	    else {
		error ("value of `%s' is not constant (", np->ident);
		expr_print (stderr, np->val);
		fprintf (stderr, ")\n");
	    }
	}
    }

    /* If symbol map is ordered, write it out now, before we deleted
       the names' values */
    if (mapfile_f)
        table_walk (&general_names, print_map);

    /* Before unrolling and merging, we must get rid of expressions
       referring to elements, because elements with nrefs>0 cannot be
       merged or removed.  Let's see where those expressions can lurk:
       (a) in elements, (b) in names, (c) in points of every segment,
       (d) in ifile.elements.  Only the (a) expressions need
       preserving; we can safely expr_free all others at this point.

       The (a) case is dealt with by expr_reduce()ing all expressions
       attached to elements: this eliminames the ELEMENT
       subexpressions since after pass_freeze() element sizes are all
       constants.  The reduction also eliminates all NAME
       subexpressions; this suggests that we can delete all general
       names after the reduction -- but we shouldn't, as the name
       identifiers may be necessary for debugging dumps.  We delete
       all expressions associated with general names, though, and thus
       handle (b).  (c) and (d) are handled by freeing points and
       ifile.elements respectively. */

    pass_reduce ();
    if (dumpset & DUMP_REDUCE)
	for (i=0; i < nsegments; i++)
	    segment_dump (stdout, ">> REDUCE ", segments[i]);
    if (traceset & TRACE_EXPR)
	expr_fsck ();

    table_walk (&general_names, name_free_value);

    for (i=0; i < nsegments; i++) {
	EXPR_FREE (segments[i]->point);
	segments[i]->point = NULL;
    }
    for (i=0; i < nifiles; i++) {
	struct ifile *ifi = ifiles[i];
	int j;
	for (j=0; j < ifi->elements_allocated; j++)
	    if (ifi->elements[j])
		EXPR_FREE (ifi->elements[j]);
	free (ifi->elements);
	ifi->elements = NULL;
	ifi->elements_allocated = 0;
    }
    if (traceset & TRACE_EXPR)
	expr_fsck ();
    /* There should be no referenced elements by now */
    if (traceset & TRACE_EXPR)
	expr_check_elements ();
    if (*error_report->n_errors)
	exit (1);


    pass_unroll ();
    if (dumpset & DUMP_UNROLL)
	for (i=0; i < nsegments; i++)
	    segment_dump (stdout, ">> UNROLL ", segments[i]);
    if (traceset & TRACE_EXPR)
	expr_fsck ();
    if (*error_report->n_errors)
	exit (1);


    pass_merge ();
    if (dumpset & DUMP_MERGE)
	for (i=0; i < nsegments; i++)
	    segment_dump (stdout, ">> MERGE ", segments[i]);
    if (traceset & TRACE_EXPR)
	expr_fsck ();
    if (*error_report->n_errors)
	exit (1);


    /* And in the end, write the output file */

    if (! (outfile_f = fopen (outfile_n, "wb")))
	fatal ("%s: %s", outfile_n, strerror (errno));

    lit_out_file = outfile_f;
    lit_out_file_name = outfile_n;

    /* Assign indices to <SEG>@@start's; a must for pass_output_segments */
    for (i=0; i < nsegments; i++)
	segments[i]->fragments[0].name->index = i;
    pass_output_segments ();
    output_stabs ();

    if (ferror (outfile_f)) {
	fatal ("%s: %s\n", outfile_n, strerror (errno));
	unlink (outfile_n);
    }

    return 0;
}


static void
output_stabs ()
{
    /* no stabs yet, sorry */
}


/* Open a new input file and add it to the ifile list */

static void
ifile_add (char *fname)
{
    struct ifile *ifi;

    if (traceset & TRACE_FILES)
	printf ("** FILES opening ifile `%s'\n", fname);

    if (VECTOR_FULL (32, nifiles))
	VECTOR_GROW (32, nifiles, struct ifile *, ifiles);
    ifi = ifiles[nifiles++] = (struct ifile *) xmalloc (sizeof (struct ifile));

    ifi->fname = fname;
    ifi->error_report_fname = fname;
    ifi->stream = &ifi->stream_proper;
    if (! (*ifi->stream = ifile_fopen (fname, "rb")))
	fatal ("cannot open `%s': %s\n", fname, strerror (errno));
    ifi->lo = 0;
    ifi->hi = 0;	/* "unknown" */
    ifi->pos = 0;
    ifi->names = NULL;
    ifi->nnames = 0;
    ifi->elements = NULL;
    ifi->elements_allocated = 0;
    ifi->wanted = 0;
}

/* Add ifile for a member of an archive which is already opened as an
   ifile `master' */

static void
ifile_add_member (struct ifile *master, long lo, long hi, char *membname)
{
    struct ifile *ifi;

    if (traceset & TRACE_FILES)
	printf ("** FILES opening ifile for archive member [0x%lx..0x%lx]\n", lo, hi);

    if (VECTOR_FULL (32, nifiles))
	VECTOR_GROW (32, nifiles, struct ifile *, ifiles);
    ifi = ifiles[nifiles++] = (struct ifile *) xmalloc (sizeof (struct ifile));
    
    ifi->fname = master->fname;
    ifi->error_report_fname = xstrcat4 (master->fname, ":", membname, "");
    ifi->stream = master->stream;	/* All members point to
					   master's stream_proper */
    ifi->lo = lo;
    ifi->hi = hi;
    ifi->pos = lo;
    ifi->names = NULL;
    ifi->nnames = 0;
    ifi->elements = NULL;
    ifi->elements_allocated = 0;
    ifi->wanted = 0;
}

/* A variation of `fopen' which will take care of "out of file
   descriptors" problem by closing stream in one of the ifiles.  */

static FILE *
ifile_fopen (char *fname, char *mode)
{
    FILE *f = fopen (fname, mode);
    int i;

    if (! f) {
	/* Probably we cannot open more files because too many ifiles
           are opened already.  Let's close something and try again.
           We choose to close the most-recently-open ifile, as it is
           going to be accessed the last. */
	for (i = nifiles; --i >= 0; ) {
	    if (*ifiles[i]->stream) {
		if (traceset & TRACE_FILES)
		    printf ("** FILES temporarily closing `%s'\n", ifiles[i]->fname);
		fclose (*ifiles[i]->stream);
		*ifiles[i]->stream = NULL;
		break;
	    }
	}
	if (i >= 0)	/* have closed something, may try again */
	    f = fopen (fname, mode);
    }

    return f;
}

/* Return nonzero if the ifile is an ar archive. */

static int
ifile_isarchive (struct ifile *ifi)
{
    char s[SARMAG];

    if (! *ifi->stream && ! (*ifi->stream = ifile_fopen (ifi->fname, "rb")))
	fatal ("cannot open `%s': %s\n", ifi->fname, strerror (errno));
    if (fseek (*ifi->stream, 0, SEEK_SET) == -1)
	fatal ("cannot fseek in `%s': %s\n", ifi->fname, strerror (errno));

    if (fread (s, 1, SARMAG, *ifi->stream) != SARMAG) {
	if (ferror (*ifi->stream))
	    fatal ("reading `%s': %s\n", ifi->fname, strerror (errno));
	return 0;
    }

    if (memcmp (s, ARMAG, SARMAG) != 0)
	return 0;

    ifi->archive = 1;
    return 1;
}

/* Scan ar file `ifi', creating new ifile for every member of the
   archive.  We should be able to read archives written by GNU ar;
   long member names are now supported */

static void
ifile_open_archive (struct ifile *ifi)
{
    struct ar_hdr header;
    char *long_name_table = NULL;
    int long_name_table_sz = 0;
    char *fname = NULL;

    if (! *ifi->stream && ! (*ifi->stream = ifile_fopen (ifi->fname, "rb")))
	fatal ("cannot open `%s': %s\n", ifi->fname, strerror (errno));
    if (fseek (*ifi->stream, SARMAG, SEEK_SET) == -1)
	fatal ("cannot fseek in `%s': %s\n", ifi->fname, strerror (errno));

    while (fread (&header, sizeof (header), 1, *ifi->stream) == 1) {
	long startpos = ftell (*ifi->stream);
	long sz;
	char *p;

	if (memcmp (header.ar_fmag, ARFMAG, sizeof (header.ar_fmag)) != 0) {
	    error ("bad file header in archive (incorrect ARFMAG)\n");
	    return;
	}

	sz = strtol (header.ar_size, &p, 10);
	if (*p != ' ' && p != &header.ar_size[sizeof (header.ar_size)]) {
	    error ("bad file header in archive (junk in ar_size)\n");
	    return;
	}

	/* Copy member name to a buffer and zero-terminate it */
	fname = xrealloc (fname, sizeof (header.ar_name)+1);
	memcpy (fname, header.ar_name, sizeof (header.ar_name));
	p = &fname[sizeof (header.ar_name)];
	do {
	    *p = '\0';
	} while (--p >= fname && *p == ' ');

	/* Check for special member names: '//' is a long name table,
           '/NNNN' is a reference to the long name table (SVR4 style
           long name handling, GNU ar uses this method).  Info source:
	   comments in GNU binutils-2.5.2/bfd/archive.c */

	if (fname[0] == '/' && fname[1] == '/' && fname[2] == '\0') {
	    if (long_name_table) {
		error ("second long name table in the archive?!\n");
		return;
	    }
	    long_name_table = xmalloc (sz+1);
	    if (fread (long_name_table, sz, 1, *ifi->stream) != 1) {
		fatal ("reading `%s': %s\n", ifi->fname, strerror (errno));
	    }
	    long_name_table[sz] = '\0'; /* make it a string so we can strchr */
	    long_name_table_sz = sz;

	    p = long_name_table;
	    while ((p = strchr (p, '\n'))) {
		*p++ = '\0';
	    }
	    
	    /* Read out the possible padding '\n' (see the comment below) */
	    if (sz&1)
		fread (&header, 1, 1, *ifi->stream);
	    
	    /* done with the table, go on to the next archive member */
	    continue;
	}
	else if (fname[0] == '/' && fname[1] >= '0' && fname[1] <= '9') {
	    unsigned offset = strtoul (&fname[1], &p, 10);
	    if (*p != '\0') {
		error ("junk long name table reference in archive\n");
		return;
	    }
	    if (offset >= long_name_table_sz) {
		error ("bad long name table reference in archive\n");
		return;
	    }
	    fname = xrealloc (fname, strlen (&long_name_table[offset])+1);
	    strcpy (fname, &long_name_table[offset]);
	}

	/* Strip SVR4 style trailing '/' (if present) from the member name */
	if ((p = strrchr (fname, '/')))
	    *p = '\0';

	/* At last, add the member to the input file list */
	ifile_add_member (ifi, startpos, startpos + sz, fname);

	/* Seek over this archve member.  Note (from BSD manpage):
	   Objects in the archive are always an even number of bytes
	   long; files which are an odd number of bytes long are
	   padded with a newline (``\n'') character, although the size
	   in the header does not reflect this. */
	if (fseek (*ifi->stream, (sz+1)&~1, SEEK_CUR) == -1)
	    fatal ("cannot fseek in `%s': %s\n", ifi->fname, strerror (errno));
    }

    if (ferror (*ifi->stream))
	fatal ("reading `%s': %s\n", ifi->fname, strerror (errno));

    if (long_name_table)
	free (long_name_table);
    if (fname)
	free (fname);
}


/* Read in nametable and needs areas from the file `ifi'.  The result
   of this is that all names from the nametable are put in the hashtable,
   need links for names are set up properly, and undefined_wanted_count
   is modified to show how many names are yet to be resolved. */

static void
ifile_read_nametable (struct ifile *ifi)
{
    int current_area = LIT_AREA_NAMETABLE;

    if (! *ifi->stream && ! (*ifi->stream = ifile_fopen (ifi->fname, "rb")))
	fatal ("cannot open `%s': %s\n", ifi->fname, strerror (errno));
    if (fseek (*ifi->stream, ifi->pos, SEEK_SET) == -1)
	fatal ("cannot fseek in `%s': %s\n", ifi->fname, strerror (errno));

    lit_in_file = *ifi->stream;
    lit_in_file_name = ifi->fname;

    for (;;) {
	long ltag;
	name np;
	long n;
	char *s;

	ltag = lit_in_number ();
	if (ltag < INT_MIN || ltag > INT_MAX) {
	    printf ("record tag 0x%lx outside bounds\n", ltag);
	    exit (1);
	}

	switch ((int) ltag) {
	case LIT_DUMMY:
	    break;

	case LIT_NAME:
	    s = lit_in_string ();
	    np = table_lookup (&general_names, s, lit_in_string_len);
	    free (s);

	    if (VECTOR_FULL (32, ifi->nnames))
		VECTOR_GROW (32, ifi->nnames, name, ifi->names);
	    ifi->names[ifi->nnames++] = np;

	    n = lit_in_number ();
	    if (n & LIT_NAME_COMMON)
		np->common = 1;

	    /* LIT_NAME_FRAGMENT ignored: we use np->fragment to
	       indicate that a fragment with this name has been read
	       in. */

	    if (n & LIT_NAME_DEFINED) {  /* name defined in this file */
		if (np->defined && ! np->common
		    && ifi->lo == 0 /* not an archive member */) {
		    warning ("redefinition of `%s' ", np->ident);
		    sources_print (stderr, np);
		    fprintf (stderr, "\n");
		}
		add_source (np, ifi);
		if (np->wanted && ! np->defined) {
		    ifi->wanted = 1;
		    if (! np->special)
			undefined_wanted_count--;
		}
		np->defined = 1;
	    }
	    else {
		add_ref (np, ifi);
	    }
	    break;

	case LIT_NEEDS:
	    n = lit_in_number ();
	    if (n < 0 || n >= ifi->nnames) {
		error ("name index %ld outside nametable\n", n);
		break;
	    }
	    np = ifi->names[(int) n];
	    while ((n = lit_in_number ()) != -1) {
		if (n < 0 || n >= ifi->nnames)
		    error ("name index %ld outside nametable\n", n);
		else {
		    if (traceset & TRACE_NEEDS)
			printf ("** NEEDS \"%s\"%s needs \"%s\"\n",
				np->ident, (np->wanted? " (wanted)": ""),
				ifi->names[(int) n]->ident);
		    add_need (np, ifi->names[(int) n]);
		    if (np->wanted)
			mark_wanted (ifi->names[(int) n]);
		}
	    }
	    break;

	case LIT_MARKER:
	    n = lit_in_number ();
	    if (n != current_area)
		error ("unexpected area code %ld\n", n);

	    lit_in_number ();	/* Eat the area length */

	    if (current_area++ >= LIT_AREA_NEEDS) {
		/* done reading name areas */
		ifi->pos = ftell (*ifi->stream);
		return;
	    }
	    break;

	case LIT_COMMENT:
	    free (lit_in_string ());
	    break;

	default:
	    error ("unexpected tag %ld in %s area\n",
		   ltag, lit_area_name (current_area));
	    return;
	}
    }
}

/* Read in wanted fragments from `ifile'.  BLOCK/DATA/CMD records in
   unwanted fragments are skipped */

static void
ifile_read_segments (struct ifile *ifi)
{
    int element_index = 0;
    segment curseg;
    name curfrag;  /* need not have one per segment, as switching
		      segment always sets up new fragment */
    name np;

    current_ifile = ifi;

    if (! *ifi->stream && ! (*ifi->stream = ifile_fopen (ifi->fname, "rb")))
	fatal ("cannot open `%s': %s\n", ifi->fname, strerror (errno));
    if (fseek (*ifi->stream, ifi->pos, SEEK_SET) == -1)
	fatal ("cannot fseek in `%s': %s\n", ifi->fname, strerror (errno));

    lit_in_file = *ifi->stream;
    lit_in_file_name = ifi->fname;

    /* Everything between the beginning of the file and the first SEGMENT
       record goes into the default segment.  Set it up as curseg */

    curseg = setup_segment (DEFAULT_SEGMENT_IDENT, 
			    sizeof (DEFAULT_SEGMENT_IDENT)-1);

    /* Switch to this file's default fragment of this segment if that
       fragment is wanted, set skipping if not. */

    if (ifi->nnames < 1) {
	error ("empty nametable: no default fragment for default segment?\n");
	return;
    }
    np = ifi->names[0];
    assert (np->elts == NULL);
    if (np->wanted) {
	if (np->fragment) {
	    error ("default fragment name already used for another fragment ?\n");
	    /* don't abort on asserts below */
	    np = table_add_noname (&general_names);
	}
	np->fragment = 1;
	assert (np->val == NULL);
	name_assign_value (np, curseg->point);
    }
    skipping = ! np->wanted;
    curfrag = np;

    /*
     * Read the SEGMENTS area of the file
     */

    for (;;) {
	long ltag;
	name np;
	expr e;
	long n;
	char *s;
	int align;

	/* Verify correctness of the expression graph on every iteration. */
	if (traceset & TRACE_EXPR)
	    expr_fsck ();

	ltag = lit_in_number ();
	if (ltag < INT_MIN || ltag > INT_MAX) {
	    printf ("record tag 0x%lx outside bounds\n", ltag);
	    exit (1);
	}

	if (ltag > 0) {  /* BLOCK type record */
	    if (skipping) {
		if (traceset & TRACE_READ)
		    printf ("** READ %s seg \"%s\" fragment \"%s\" elt %d skipped\n",
			    ifi->error_report_fname, curseg->name->ident,
			    curfrag->ident, element_index);
		element_index++;
		free (lit_in_block ((size_t) ltag));
	    }
	    else {
		if (traceset & TRACE_READ)
		    printf ("** READ %s seg \"%s\" fragment \"%s\" elt %d read\n",
			    ifi->error_report_fname, curseg->name->ident,
			    curfrag->ident, element_index);
		add_element (curseg, ELEMENT_BLOCK);

		e = element_by_index (element_index++);
		e->u.element = curseg->curel;
		EXPR_FREE (e);

		curseg->curel->nrefs++;  /* comment in expr_element */
		curseg->curel->u.block.ptr = lit_in_block ((size_t) ltag);
		curseg->curel->u.block.allocated = (size_t) ltag;
		EXPR_FREE (curseg->curel->size);
		curseg->curel->size = expr_const (ltag);
		expr_plus_element (&curseg->point, curseg->curel);
		if (! curfrag->elts)
		    curfrag->elts = curseg->curel;
	    }
	    continue;
	}

    tagswitch:

	if (traceset & TRACE_POINT) {
	    printf ("** POINT %s ", curseg->name->ident);
	    expr_print (stdout, curseg->point);
	    printf ("\n");
	}

	switch ((int) ltag) {
	case LIT_DUMMY:
	    break;

	case LIT_SEGMENT:
	    s = lit_in_string ();
	    curseg = setup_segment (s, lit_in_string_len);
	    free (s);

	    n = lit_in_number ();
	    if (n < 0 || n >= ifi->nnames)
		error ("name index %ld outside nametable\n", n);
	    np = ifi->names[n];
	    assert (np->elts == NULL);
	    if (np->wanted) {
		if (np->fragment)
		    error ("default fragment name already used for another fragment?\n");
		assert (np->val == NULL);
		name_assign_value (np, curseg->point);
	    }
	    skipping = ! np->wanted;
	    curfrag = np;
	    break;
	    
	case LIT_FRAGMENT:
	    n = lit_in_number ();
	    if (n < 0 || n >= ifi->nnames) {
		error ("name index %ld outside nametable\n", n);
		lit_in_number ();
		break;
	    }
	    np = ifi->names[n];

	    if (np->special)
		error ("user fragment may not be named `%s'\n", np->ident);

	    align = lit_in_number ();
	    if (align & (align-1)) {
		/* not a power of 2?  some code relies on it... */
		warning ("suspicious alignment %ld for fragment `%s'; changed to 4\n",
			 align, np->ident);
		align = 4;
	    }

	    if (np->common && np->fragment) {
		/* 2nd (3rd, ...) instance of a common fragment.  Eat
                   it, and correct size of the first one, if nec.  */

		if (np->align != align) {
		    /* We could handle alignment mismatch... later */
		    error ("alignment mismatch (was %d, new %d) for COMMON block `%s' ",
			   np->align, align, np->ident);
		    sources_print (stderr, np);
		    fprintf (stderr, "\n");
		}

		if ((ltag = lit_in_number ()) != LIT_DATA) {
		    error ("COMMON block has tag %ld, not LIT_DATA?\n", ltag);
		    /* don't bother trying to recover from this */
		    return;
		}

		element_index++;

		if ((n = lit_in_number ()) != 1)
		    error ("illegal data width %ld for COMMON block `%s'\n",
			   n, np->ident);

		e = lit_in_expr ();
		if (! EXPR_ISCONST (e) || e->u.constval != 0)
		    error ("illegal data value %ld for COMMON block `%s'\n",
			   e->u.constval, np->ident);
		EXPR_FREE (e);

		expr_free (lit_in_expr ());	/* data count */

		e = lit_in_expr ();	/* data size */
		assert (np->elts);
		if (! expr_equal (np->elts[0].size, e)) {
		    /* Should maximize rather than swear... later.
		       ??? What if it has more than one element?
		       It shouldn't, but? */
		    error ("size mismatch (was ");
		    expr_print (stderr, np->elts[0].size);
		    fprintf (stderr, "  new ");
		    expr_print (stderr, e);
		    fprintf (stderr, ") for COMMON block `%s' ", np->ident);
		    sources_print (stderr, np);
		    fprintf (stderr, "\n");
		}
		EXPR_FREE (e);
		
		while ((ltag = lit_in_number ()) == LIT_DUMMY)
		    ;
		if (ltag > 0 /*BLOCK*/ || ltag == LIT_DATA || ltag == LIT_CMD) {
		    error ("more than one element in COMMON fragment `%s' ",
			   np->ident);
		    sources_print (stderr, np);
		    fprintf (stderr, "\n");
		    return;
		}
		goto tagswitch;
	    }

	    if (np->wanted) {
		if (np->fragment) {
		    /* redefinition -- skip silently.  We must have
		       already warned about it while reading nametables */
		    skipping = 1;
		    break;
		}
		assert (np->elts == NULL);
		np->fragment = 1;
		if (align > 1) {
		    expr temp;
		    emit_align (curseg, align, temp = expr_const (0));
		    EXPR_FREE (temp);
		}
		assert (np->val == NULL);
		name_assign_value (np, curseg->point);
		/* Do the assignment *after* name_assign_value; we
                   know point is well aligned, but name_assing_value
                   may turn out not smart enough to recognize this. */
		np->align = align;
	    }
	    skipping = ! np->wanted;
	    curfrag = np;
	    break;

	case LIT_DATA:
	    if (skipping) {
		if (traceset & TRACE_READ)
		    printf ("** READ %s seg \"%s\" fragment \"%s\" elt %d skipped\n",
			    ifi->error_report_fname, curseg->name->ident,
			    curfrag->ident, element_index);
		element_index++;
		lit_in_number ();
		expr_free (lit_in_expr ());
		expr_free (lit_in_expr ());
		expr_free (lit_in_expr ());
	    }
	    else {
		if (traceset & TRACE_READ)
		    printf ("** READ %s seg \"%s\" fragment \"%s\" elt %d read\n",
			    ifi->error_report_fname, curseg->name->ident,
			    curfrag->ident, element_index);
		add_element (curseg, ELEMENT_DATA);

		e = element_by_index (element_index++);
		e->u.element = curseg->curel;
		curseg->curel->nrefs++;  /* comment in expr_element */
		EXPR_FREE (e);

		n = lit_in_number ();
		if (n != 1 && n != 2 && n != 4)
		    error ("illegal data width %ld\n", n);
		curseg->curel->u.data.width = n;
		curseg->curel->u.data.value = lit_in_expr ();
		curseg->curel->u.data.count = lit_in_expr ();
		curseg->curel->size = lit_in_expr ();
		expr_plus_element (&curseg->point, curseg->curel);
		if (! curfrag->elts)
		    curfrag->elts = curseg->curel;
	    }
	    break;

	case LIT_CMD:
	    if (skipping) {
		if (traceset & TRACE_READ)
		    printf ("** READ %s seg \"%s\" fragment \"%s\" elt %d skipped\n",
			    ifi->error_report_fname, curseg->name->ident,
			    curfrag->ident, element_index);
		element_index++;
		lit_in_number ();
		expr_free (lit_in_expr ());
		expr_free (lit_in_expr ());
	    }
	    else {
		if (traceset & TRACE_READ)
		    printf ("** READ %s seg \"%s\" fragment \"%s\" elt %d read\n",
			    ifi->error_report_fname, curseg->name->ident,
			    curfrag->ident, element_index);
		add_element (curseg, ELEMENT_CMD);

		e = element_by_index (element_index++);
		e->u.element = curseg->curel;
		curseg->curel->nrefs++;  /* comment in expr_element */
		EXPR_FREE (e);

		n = lit_in_number ();
		if (n < 0 || n > 0xf0 || n & 0x0f)
		    error ("illegal opcode 0x%lx\n", n);
		curseg->curel->u.cmd.opcode = n;
		curseg->curel->u.cmd.arg = lit_in_expr ();
		curseg->curel->size = lit_in_expr ();
		expr_plus_element (&curseg->point, curseg->curel);
		if (! curfrag->elts)
		    curfrag->elts = curseg->curel;
	    }
	    break;
	    

	case LIT_MARKER:
	    n = lit_in_number ();
	    if (n != LIT_AREA_SEGMENTS)
		error ("unexpected area code %ld\n", n);

	    lit_in_number ();	/* Eat the area length */

	    /* Check whether all elements referenced has been read in */
	    {
		int i;
		for (i=0; i < ifi->elements_allocated; i++) {
		    if (ifi->elements[i] && ! ifi->elements[i]->u.element)
			error ("element [%d] has been referenced but not defined in any wanted fragment\n", i);
		}
	    }

	    ifi->pos = ftell (*ifi->stream);
	    return;

	case LIT_COMMENT:
	    free (lit_in_string ());
	    break;

	default:
	    error ("unexpected tag %ld in the SEGMENTS area\n", ltag);
	    return;
	}
    }
    /* NOTREACHED */
}

/* Create the segment `seg_ident' if it is not yet existing
   and return the segment pointer. */

static segment
setup_segment (char *seg_ident, int seg_ident_len)
{
    name segname = table_lookup (&segment_names, seg_ident, seg_ident_len);
    segment newseg;

    if (segname->val)
	newseg = (segment) segname->val;
    else {  /* Need to create new segment */
	/* make default fragment name */
	char *p = xstrcat4 (segname->ident, "@@start", "", "");
	name np = table_lookup (&general_names, p, strlen (p));
	free (p);

	np->fragment = 1;
	np->loadtime = 1;  /* XXX@@start may stay undefined until loading */
	name_set_align (np, segment_align);
	newseg = add_segment (segname, np);
	assert (np->elts == NULL);
    }

    return newseg;
}


/* Read in name definitions from `ifile'. */

static void
ifile_read_definitions (struct ifile *ifi)
{
    current_ifile = ifi;
    skipping = 0;

    if (! *ifi->stream && ! (*ifi->stream = ifile_fopen (ifi->fname, "rb")))
	fatal ("cannot open `%s': %s\n", ifi->fname, strerror (errno));
    if (fseek (*ifi->stream, ifi->pos, SEEK_SET) == -1)
	fatal ("cannot fseek in `%s': %s\n", ifi->fname, strerror (errno));

    lit_in_file = *ifi->stream;
    lit_in_file_name = ifi->fname;

    for (;;) {
	long ltag;
	name np;
	long n;
	expr e;

	/* Verify correctness of the expression graph on every iteration. */
	if (traceset & TRACE_EXPR)
	    expr_fsck ();

	ltag = lit_in_number ();
	if (ltag < INT_MIN || ltag > INT_MAX) {
	    printf ("record tag 0x%lx outside bounds\n", ltag);
	    exit (1);
	}

	switch ((int) ltag) {
	case LIT_DUMMY:
	    break;

	case LIT_NAMEDEF:
	    n = lit_in_number ();
	    if (n < 0 || n >= ifi->nnames) {
		error ("name index %ld outside nametable\n", n);
		expr_free (lit_in_expr ());
		break;
	    }
	    np = ifi->names[n];

	    e = lit_in_expr ();
	    if (np->wanted || np->special) {
		if (! np->val)
		    name_assign_value (np, e);
		else {
		    if (! expr_equal (e, np->val)) {
			warning ("non-identical redefinition of `%s':\n");
			fprintf (stderr, "  was ");
			expr_print (stderr, np->val);
			fprintf (stderr, "  new ");
			expr_print (stderr, e);
			fprintf (stderr, "\n");
		    }
		}
	    }
	    EXPR_FREE (e);

	    if (np->special == NAME_SPECIAL_SIZE) {
		error ("assignment to `%s' is not allowed\n", np->ident);
		if (np->val) {
		    EXPR_FREE (np->val);
		    np->val = NULL;
		}
	    }

	    break;
	    
	case LIT_MARKER:
	    n = lit_in_number ();
	    if (n != LIT_AREA_DEFINITIONS)
		error ("unexpected area code %ld\n", n);

	    lit_in_number ();	/* Eat the area length */
	    return;

	case LIT_COMMENT:
	    free (lit_in_string ());
	    break;

	default:
	    error ("unexpected tag %ld in the DEFINITIONS area\n", ltag);
	    return;
	}
    }
    /* NOTREACHED */
}


/* Mark `np', and all names it needs, as "wanted" */

static void
mark_wanted (name np)
{
    static int level = -1;
    level++;
    if (! np->wanted) {
	if (traceset & TRACE_NEEDS)
	    printf ("** NEEDS %*s\"%s\" marked wanted\n", level*2,
		    "", np->ident);
	np->wanted = 1;
	if (np->defined) {
	    struct ifile *ifi = first_source (np);
	    ifi->wanted = 1;
	}
	else {
	    if (! np->special) 
		undefined_wanted_count++;
	}
	if (np->needs) {
	    name *npp;
	    for (npp = np->needs; *npp; npp++)
		mark_wanted (*npp);
	}
    }
    level--;
}

static void
add_source (name np, struct ifile *ifi)
{
    if (np->source == NULL) {
	np->source = (struct ifile **) ifi;
	return;
    }
    else if (! np->multisrc) {
	struct ifile *old = (struct ifile *) np->source;
	np->source = NULL;
	VECTOR_GROW (4, 0, struct ifile *, np->source);
	np->source[0] = old;
	np->source[1] = ifi;
	np->source[2] = NULL;
	np->multisrc = 1;
    }
    else {
	int nsources = 2;  /* if multisrc, we sure have at least two sources */
	while (np->source[nsources])
	    nsources++;
	if (VECTOR_FULL (4, nsources + 1))
	    VECTOR_GROW (4, nsources + 1, struct ifile *, np->source);
	np->source[nsources] = ifi;
	np->source[nsources + 1] = NULL;
    }
}

struct ifile *
first_source (name np)
{
    if (np->multisrc)
	return np->source[0];
    else
	return (struct ifile *) np->source;
}

/* Like add_source, builds list of ifiles that reference this name in
   np->refs. */

static void
add_ref (name np, struct ifile *ifi)
{
    if (np->refs == NULL) {
	np->refs = (struct ifile **) ifi;
	return;
    }
    else if (! np->multiref) {
	struct ifile *old = (struct ifile *) np->refs;
	np->refs = NULL;
	VECTOR_GROW (4, 0, struct ifile *, np->refs);
	np->refs[0] = old;
	np->refs[1] = ifi;
	np->refs[2] = NULL;
	np->multiref = 1;
    }
    else {
	int nrefs = 2;  /* if multiref, we sure have at least two refs */
	while (np->refs[nrefs])
	    nrefs++;
	if (VECTOR_FULL (4, nrefs + 1))
	    VECTOR_GROW (4, nrefs + 1, struct ifile *, np->refs);
	np->refs[nrefs] = ifi;
	np->refs[nrefs + 1] = NULL;
    }
}

/* Return printable, comma-separated list of filenames where `np' is
   defined */

static void
sources_print (FILE *stream, name np)
{
    struct ifile **pp;

    if (np->source) {
	fprintf (stream, "defined in");
	if (! np->multisrc)
	    fprintf (stream, " %s", ((struct ifile *) np->source)->fname);
	else {
	    for (pp = np->source; *pp; pp++)
		fprintf (stream, " %s", (*pp)->fname);
	}
	if (np->refs)
	    fprintf (stream, ",");
    }

    if (np->refs) {
	fprintf (stream, "referred from");
	if (! np->multiref)
	    fprintf (stream, " %s", ((struct ifile *) np->refs)->error_report_fname);
	else {
	    for (pp = np->refs; *pp; pp++)
		fprintf (stream, " %s", (*pp)->error_report_fname);
	}
    }
}


name
name_by_index (long n)
{
    struct ifile *ifi = current_ifile;
    
    if (n < 0 || n >= ifi->nnames) {
	error ("name index %ld outside nametable\n", n);
	return table_add_noname (&general_names);
    }
    return ifi->names[n];
}

/* Return an ELEMENT expression for an element with index `n'.  Called
   from lit_in_expr.  The reason why we cannot return the element
   proper is that the element may be unavailable yet (being defined
   later in the input file). */

expr
element_by_index (long n)
{
    struct ifile *ifi = current_ifile;
    
    if (n < 0)
	error ("illegal element index %ld\n", n);

    /* In skipping mode, ifile_read_segments still uses lit_in_expr,
       since it is the only way to read an expr; the expr read is then
       expr_free()ed.  However, we don't want elements in the skipped
       expressions to get into the element vector.  */
    if (skipping) {
	static expr dummy_element_expr = NULL;
	if (! dummy_element_expr) {
	    dummy_element_expr = expr_element (NULL);
	    /* should tie the expr somewhere, or else expr_fsck will
	       count it orphaned */
	    table_add_noname (&general_names)->val = dummy_element_expr;
	}
	return expr_share (dummy_element_expr);
    }
    
    if (ifi->elements_allocated <= n) {
	ifi->elements = (expr *) xrealloc (ifi->elements,
					   (n+20) * sizeof (expr));
	memset (&ifi->elements[ifi->elements_allocated], 0,
		(n+20 - ifi->elements_allocated) * sizeof (expr));
	ifi->elements_allocated = n+20;
    }

    if (ifi->elements[n] == NULL)
	ifi->elements[n] = expr_element (NULL);
    
    return expr_share (ifi->elements[n]);
}

/* Issue an error message if name is not defined.  Used over the whole
   namespace once the nametable area parsing is complete. */

static void *
report_undefined (name np)
{
    if (np->wanted && !np->defined && !np->special) {
	error ("undefined symbol `%s' ", np->ident);
	sources_print (stderr, np);
	fprintf (stderr, "\n");
    }
    return NULL;
}

/* Issue an error message for every name which is wanted but has no
   value, save for *@@start, which are marked `loadtime' and are
   allowed to have no value until loading. */

static void *
check_defined (name np)
{
    if (np->wanted && !np->val && !np->loadtime) {
	if (np->defined)
	    error ("not actually defined symbol `%s' ", np->ident);
	else
	    error ("undefined symbol `%s' ", np->ident);
	sources_print (stderr, np);
	fprintf (stderr, "\n");
    }
    return NULL;
}


/* Delete the expression which is the value of the name `np'. */

static void *
name_free_value (name np)
{
    if (np->val) {
	EXPR_FREE (np->val);
	np->val = NULL;
    }
    return NULL;
}

/* Print a symbol map line for the name `np' */

static void *
print_map (name np)
{
    int save_expr_print_long;

    /* We skip symbols that do not go into the output file, fragments
       and noname symbols */
    if (np->wanted && np->val && np->ident[0]) {

        /* Print all elsum members, not just the number of them */
	save_expr_print_long = expr_print_long;
	expr_print_long = 1;

        fprintf (mapfile_f, "%-30s\t", np->ident);
	expr_reduce (np->val);
	expr_print (mapfile_f, np->val);
        fprintf (mapfile_f, "\n");

	expr_print_long = save_expr_print_long;
    }
    return NULL;
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
    }
    for (i=0; i < nifiles; i++) {
	struct ifile *ifi = ifiles[i];
	int j;
	for (j=0; j < ifi->elements_allocated; j++)
	    if (ifi->elements[j])
		expr_count_refs (ifi->elements[j]);
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

static void
expr_check_elements ()
{
#ifdef EXPR_DEBUG
    expr e;

    for (e = expr_list->forw; e != expr_list; e = e->forw) {
	if (e->tag == EXPR_ELEMENT) {
	    if (! e->u.element)
		printf ("** EXPR %p u.element is NULL\n", e);
	    else if (e->u.element->nrefs) {
		printf ("** EXPR %p refers to elt %p\n", e, e->u.element);
	    }
	}
    }
#endif
}
