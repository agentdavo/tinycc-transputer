/* $Id: litdump.c,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#include "litdump.h"

static void dump (char *p, size_t len);


name *names = NULL;
int nnames = 0;

element *elements = NULL;
int nelements = 0;

static char *myname = "litdump";

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
	fprintf (stderr, "This is litdump version %s\n", version_string);
	fprintf (stderr, "Usage: %s [options] FILE\n", myname);
	fprintf (stderr, "%s --help for options list\n", myname);
	break;

    case USAGE_OPTIONS:
	fprintf (stderr, "This is litdump version %s\n", version_string);
	fprintf (stderr, "Usage: %s [options] OBJFILE\n", myname);
	fprintf (stderr, "Options are:\n");
	fprintf (stderr, "%s", option_list);
	fprintf (stderr, "\n");
	break;

    default:
	abort ();
    }

    exit (exit_code);
}

int
main (int argc, char **argv)
{
    int disflag = 0;
    static int dummy = 0;
    int c;
    expr point = NULL;

    /* List of segments in which we want disassembling enabled */
    char **dissegments = NULL;
    int ndissegments = 0;

    table_init (&general_names, 1024);

    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Find out how we are named on this system */
    if ((myname = strrchr (argv[0], '/') + 1) == NULL + 1)
	myname = argv[0];

    /* Parse options */
    while ((c = getopt_long (argc, argv, "o:Ed:",
			     opttab, &dummy)) != -1) {
	switch (c) {
	case '?':		/* an illegal option */
	    usage ("", USAGE_GENERAL, 1);

	case 301:		/* "help" */
	    usage ("", USAGE_OPTIONS, 0);

	case 'o':
	    if (! freopen (optarg, "w", stdout)) {
		perror (optarg);
		exit (1);
	    }
	    break;

	case 'E':
	    expr_print_long++;
	    break;

	case 'd':
	    if (VECTOR_FULL (16, ndissegments))
		VECTOR_GROW (16, ndissegments, char *, dissegments);
	    dissegments[ndissegments++] = optarg;
	    if (strcmp (optarg, "head") == 0) {
		point = expr_name (name_by_index (0));
		disflag = 1;
	    }
	    break;

	default:
	    abort ();
	}
    }

    if (optind >= argc)
	usage ("No input file given\n", USAGE_GENERAL, 1);

    lit_in_file_name = argv[optind];
    if (! (lit_in_file = fopen (lit_in_file_name, "rb"))) {
	perror (lit_in_file_name);
	exit (1);
    }

    for (;;) {
	int c;
	int i;
	long n;
	long tag;
	expr e;
	char *s;

	/* Probe for EOF.  lit_in_* routines give fatal error on EOF
	   since they expect the caller to stop on a marker.  But here
	   we want to dump all records up to EOF, hence this trick. */

	if ((c = getc (lit_in_file)) == EOF)
	    break;
	ungetc (c, lit_in_file);

	printf ("%06ld: ", ftell (lit_in_file));

	tag = lit_in_number ();
	if (tag < INT_MIN || tag > INT_MAX) {
	    printf ("record tag 0x%lx outside bounds\n", tag);
	    exit (1);
	}

	if (tag > 0) {
	    printf ("element #%d BLOCK(%ld)\n", nelements, tag);

	    if (VECTOR_FULL (32, nelements))
		VECTOR_GROW (32, nelements, element, elements);
	    elements[nelements++] = element_alloc (ELEMENT_BLOCK);
	    elements[nelements-1]->index = nelements-1;
	    elements[nelements-1]->size = expr_const (tag);

	    s = lit_in_block ((size_t) tag);
	    if (! disflag)
		dump (s, (size_t) tag);
	    else {
		unsigned char *b = s;
		int n = tag;
		
		while (n > 0) {
		    struct disassemble *das = disassemble (b, n);

		    printf ("\t");
		    expr_print (stdout, point);
		    printf (":\n");

		    expr_plus_constant (&point, das->size);

		    printf ("\t%23s %s ", das->dump, das->mnem);
		    if (das->hasarg) {
			expr e = expr_const (das->arg);
			if (das->pcrel)
			    expr_asgop (EXPR_PLUS, &e, point);
			expr_print (stdout, e);
			expr_free (e);
		    }
		    printf ("\n");
		    n -= das->size;
		    b += das->size;
		}
	    }
	    free (s);
	    continue;
	}

	/* Now negative tags */

	switch ((int) tag) {
	case LIT_DUMMY:
	    printf ("DUMMY\n");
	    break;

	case LIT_DATA:
	    printf ("element #%d DATA/%ld", nelements, lit_in_number ());

	    if (VECTOR_FULL (32, nelements))
		VECTOR_GROW (32, nelements, element, elements);
	    elements[nelements++] = element_alloc (ELEMENT_DATA);
	    elements[nelements-1]->index = nelements-1;

	    printf ("\n\tvalue ");
	    expr_print (stdout, e = lit_in_expr ());
	    expr_free (e);

	    printf ("\n\tcount ");
	    expr_print (stdout, e = lit_in_expr ());
	    expr_free (e);

	    printf ("\n\tsize ");
	    expr_print (stdout, e = lit_in_expr ());
	    elements[nelements-1]->size = e;

	    printf ("\n");
	    break;

	case LIT_CMD:
	    {
		expr arg, size;

		n = lit_in_number ();
		arg = lit_in_expr ();
		size = lit_in_expr ();

		printf ("element #%d CMD 0x%02lx", nelements, n);

		if (VECTOR_FULL (32, nelements))
		    VECTOR_GROW (32, nelements, element, elements);
		elements[nelements++] = element_alloc (ELEMENT_CMD);
		elements[nelements-1]->index = nelements-1;
		elements[nelements-1]->size = size;

		if (! disflag) {
		    printf ("\n\targ ");
		    expr_print (stdout, arg);

		    printf ("\n\tsize ");
		    expr_print (stdout, size);
		}
		else {
		    unsigned char b[1] = { (unsigned char) n };
		    struct disassemble *das = disassemble (b, 1);

		    printf ("\n\t");
		    expr_print (stdout, point);
		    printf (":\n");

		    expr_plus_element (&point, elements[nelements-1]);

		    printf (" %23s %s ", "", das->mnem);
		    assert (das->hasarg);
		    if (das->pcrel) {
			expr pcrel_arg = expr_binop (EXPR_PLUS, point, arg);
			expr_print (stdout, pcrel_arg);
			expr_free (pcrel_arg);
		    }
		    else
			expr_print (stdout, arg);

		}
		printf ("\n");
		expr_free (arg);
		expr_free (size);
	    }
	    break;

	case LIT_NAME:
	    s = lit_in_string ();
	    n = lit_in_number ();

	    if (VECTOR_FULL (32, nnames))
		VECTOR_GROW (32, nnames, name, names);
	    names[nnames++] = table_lookup (&general_names, s, lit_in_string_len);
	    names[nnames-1]->index = nnames-1;

	    printf ("NAME:%d is ", nnames-1);
	    expr_print (stdout, e = expr_name (name_by_index (nnames-1)));
	    expr_free (e);
	    printf (",");
	    if (n & LIT_NAME_COMMON)
		printf (" common");
	    if (n & LIT_NAME_FRAGMENT)
		printf (" fragment");
	    printf (" defined %s",
		    (n & LIT_NAME_DEFINED)? "here": "elsewhere");
	    if (n & ~(LIT_NAME_DEFINED|LIT_NAME_FRAGMENT|LIT_NAME_COMMON))
		printf ("\7(more flags? 0x%lx)", n);
	    printf ("\n");
	    free (s);
	    break;
	    
	case LIT_NEEDS:
	    n = lit_in_number ();

	    printf ("NEED: ");
	    expr_print (stdout, e = expr_name (name_by_index (n)));
	    expr_free (e);

	    printf (" needs");
	    while ((n = lit_in_number ()) != -1) {
		printf (" ");
		expr_print (stdout, e = expr_name (name_by_index (n)));
		expr_free (e);
	    }
	    printf ("\n");
	    break;
	    
	case LIT_SEGMENT:
	    s = lit_in_string ();
	    n = lit_in_number ();

	    printf ("SEGMENT \"%s\", default fragment ", s);
	    expr_print (stdout, e = expr_name (name_by_index (n)));
	    expr_free (e);
	    printf ("\n");

	    disflag = 0;
	    for (i=0; i < ndissegments; i++) {
		if (strcmp (dissegments[i], s) == 0) {
		    if (point)
			expr_free (point);
		    point = expr_name (name_by_index (n));
		    disflag = 1;
		}
	    }
	    free (s);
	    break;
	    
	case LIT_FRAGMENT:
	    n = lit_in_number ();

	    printf ("FRAGMENT ");
	    expr_print (stdout, e = expr_name (name_by_index (n)));
	    expr_free (e);

	    printf (", align %ld\n", lit_in_number ());

	    if (disflag) {
		if (point)
		    expr_free (point);
		point = expr_name (name_by_index (n));
	    }
	    break;

	case LIT_NAMEDEF:
	    n = lit_in_number ();

	    printf ("NAMEDEF ");
	    expr_print (stdout, e = expr_name (name_by_index (n)));
	    expr_free (e);

	    printf (" has value ");
	    expr_print (stdout, e = lit_in_expr ());
	    expr_free (e);
	    printf ("\n");
	    break;
	    
	case LIT_MARKER:
	    n = lit_in_number ();
	    printf ("MARKER: end of area typecode %ld (%s), area length %ld\n",
		    n, lit_area_name (n), lit_in_number ());
	    break;

	case LIT_COMMENT:
	    printf ("COMMENT: \"%s\"\n", s = lit_in_string ());
	    free (s);
	    break;

	default:
	    printf ("unknown record tag 0x%lx\n", tag);
	    exit (1);
	}
    }

    return 0;
}

name
name_by_index (long index)
{
    char id[20];
    if ((unsigned)index < nnames)
	return names[index];
    else
	return name_alloc (id, sprintf (id, "name:%ld", index));
}

expr
element_by_index (long index)
{
    if ((unsigned)index < nelements)
	return expr_element (elements[index]);
    else {
	element el = element_alloc (ELEMENT_DATA);
	el->index = index;
	el->size = expr_range (0,0);
	return expr_element (el);
    }
}

static void
dump (char *p, size_t len)
{
    int i;

    for (i=0; i < len; i++) {
	if (i % 16 == 0)
	    printf (i == 0? "\t": "\n\t");
	printf ("%02x%c", (unsigned char) p[i], (i % 16 == 7) ? '-': ' ');
    }
    printf ("\n");
}

/* We have to define this stub since now expr_fsck is also called from
   expr.c.  (If we linked using lit, we wouldn't have this prob! :-) */
void
expr_fsck ()
{
}
