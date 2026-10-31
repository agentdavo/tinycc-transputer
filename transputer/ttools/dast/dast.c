/* $Id: dast.c,v 1.1.1.1 1995/12/22 12:24:39 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#include "shared.h"
#include "disassemble.h"

char * infile_n = "stdin";

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
	fprintf (stderr, "This is dast version %s\n", version_string);
	fprintf (stderr, "Usage: %s [options] [BINFILE]\n", myname);
	fprintf (stderr, "%s --help for options list\n", myname);
	break;

    case USAGE_OPTIONS:
	fprintf (stderr, "This is dast version %s\n", version_string);
	fprintf (stderr, "Usage: %s [options] [BINFILE]\n", myname);
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
    static int dummy = 0;
    unsigned long skip = 0;
    unsigned long point = 0;
    char *p;
    int c;
    int i;
    char buf[BUFSIZ + CMD_MAXLEN];
    int bufcount;
    unsigned char *bufp;

    /* Find out how we are named on this system */
    if ((myname = strrchr (argv[0], '/') + 1) == NULL + 1)
	myname = argv[0];

    /* Parse options */
    while ((c = getopt_long (argc, argv, "o:s:O:",
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

	case 's':
	    skip = strtoul (optarg, &p, 0);
	    if (*p != '\0') {
		fprintf (stderr, "Bad argument to --skip: `%s'\n", optarg);
		exit (1);
	    }
	    break;

	case 'O':
	    point = strtoul (optarg, &p, 0);
	    if (*p != '\0') {
		fprintf (stderr, "Bad argument to --origin: `%s'\n", optarg);
		exit (1);
	    }
	    break;

	default:
	    abort ();
	}
    }

    if (optind < argc) {
	infile_n = argv[optind];
	if (! freopen (infile_n, "rb", stdin)) {
	    perror (optarg);
	    exit (1);
	}
	if (++optind < argc)
	    usage ("Too many input files given\n", USAGE_GENERAL, 1);
    }

    
    if (skip) {
	if (fseek (stdin, skip, 1) == -1) {
	    /* should be a pipe... skip by read */
	    for ( ; skip > 0; skip -= sizeof (buf))
		fread (buf, 1, skip < BUFSIZ? skip: BUFSIZ, stdin);
	}
    }

    bufp = buf;
    bufcount = fread (bufp, 1, BUFSIZ, stdin);

    while (bufcount > 0) {
	struct disassemble *das;

	if (bufcount < CMD_MAXLEN && ! feof (stdin)) {
	    memcpy (buf, bufp, bufcount);
	    bufp = buf;
	    bufcount += fread (&buf[bufcount], 1, BUFSIZ, stdin);
	    if (ferror (stdin)) {
		perror (infile_n);
		exit (1);
	    }
	}

	das = disassemble (bufp, bufcount);

	printf ("0x%08lx: ", point);
	point += das->size;

	for (i = 0; i < das->size; i++)
	    printf ("%c", isprint (bufp[i])? bufp[i]: '.');
	printf ("%*s", CMD_MAXLEN - i, "");

	printf (" %-23s %s", das->dump, das->mnem);
	if (das->hasarg) {
	    if (das->pcrel)
		printf (" 0x%08lx", point + das->arg);
	    else {
		printf (" %ld", das->arg);
		if (das->arg > 10 || das->arg < -2)
		    printf (" (0x%lx)", das->arg);
	    }
	}
	printf ("\n");

	bufp += das->size;
	bufcount -= das->size;
    }

    return 0;
}
