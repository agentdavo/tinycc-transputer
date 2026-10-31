/* $Id: crossload.c,v 1.1.1.1 1995/12/22 12:24:40 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

/* Intentionally simple cross-loader for absolute programs: builds
   memory image in a file which you can burn in ROM, etc. */


/* Max size of memory image this program can build.  We malloc a
   buffer of this size; feel free to increase if you have enough
   virtual memory. */
#define MAX_IMAGE_SIZE  (512*1024)


#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>	/* for getopt */
#include "litload.h"

char *myname;
char *out_n = "l.out";
FILE *out_f;
char *in_n;
FILE *in_f;

void
usage ()
{
    fprintf (stderr, "Usage: %s [-o IMAGEFILE|-] LITFILE\n", myname);
    exit (1);
}

void *
xmalloc (size_t size)
{
    void *p = malloc (size);

    if (!p) {
	fprintf (stderr, "malloc failed -- can't go on\n");
	exit (1);
    }

    return p;
}


int
main (int argc, char **argv)
{
    int c;
    char *buf, *p;

    /* Find out how we are named on this system */
    if ((myname = strrchr (argv[0], '/') + 1) == NULL + 1)
	myname = argv[0];

    /* Parse options */
    while ((c = getopt(argc, argv, "o:")) != -1) {
	switch (c) {
	case 'o':
	    out_n = optarg;
	    break;

	default:
	    usage ();
	}
    }

    if (optind != argc-1)
	usage ();

    /* Open input file */
    in_n = argv[optind];
    if (! (in_f = fopen (in_n, "r"))) {
	perror (in_n);
	exit (1);
    }

    /* Open output file */
    if (strcmp (out_n, "-") == 0) {
	out_n = "stdout";
	out_f = stdout;
    }
    else {
	if (! (out_f = fopen (out_n, "w"))) {
	    perror (out_n);
	    exit (1);
	}
    }

    buf = xmalloc (MAX_IMAGE_SIZE);
    p = buf;
    for (;;) {
	int more;
	more = lit_load_segment (p, &buf[MAX_IMAGE_SIZE] - p, NULL, 0);
	p += lit_load_segsize;
	if (!more)
	    break;
    }

    if (fwrite (buf, 1, p-buf, out_f) != p-buf) {
	perror (out_n);
	exit (1);
    }

    return 0;
}

void
lit_load_fatal (int line, char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);

    vfprintf(stderr, fmt, ap);
    exit (1);
}

int
lit_load_in_byte ()
{
    int c = getc (in_f);
    if (c == EOF) {
	if (ferror (in_f))
	    perror (in_n);
	else
	    fprintf (stderr, "unexpected EOF in `%s'\n", in_n);
	exit (1);
    }
    return c;
}

void
lit_load_in_block (char *area, int size)
{
    if (fread (area, 1, size, in_f) != size) {
	if (ferror (in_f))
	    perror (in_n);
	else
	    fprintf (stderr, "unexpected EOF in `%s'\n", in_n);
	exit (1);
    }
}

