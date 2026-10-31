/* $Id: mktabs.c,v 1.1.1.1 1995/12/22 12:24:39 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

/*
 * Convert intstruction set definition in scan.l into disassembler
 * look-at tables.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bailout ();
void add_entry (char ***tabp, int *tabszp, int opcode, char *mnem);
void *xmalloc (size_t size);
void *xrealloc (void *old, size_t size);

int lineno = 0;
char *nullstr = "NULL";


int
main ()
{
    int opcode;
    char s[1024];
    char *p;
    char *mnem;

    char **tab_direct = NULL;
    int ntab_direct = 0;
    char **tab_indir = NULL;
    int ntab_indir = 0;
    char **tab_indir_negative = NULL;
    int ntab_indir_negative = 0;
    char **tab_fpu = NULL;
    int ntab_fpu = 0;
    char tab_pcrel[256];

    memset (tab_pcrel, 0, sizeof (tab_pcrel));


    while (fgets (s, sizeof (s), stdin)) {

	lineno++;

	/* "gt" 		CMDDEF (0x09, TOK_CMD_INDIR, CORE) */

	if (*s == '"' && strstr (s, "CMDDEF")) {

	    if (! (p = strtok (s, " \t")))
		bailout ();
	    mnem = xmalloc (strlen (p)+1);
	    strcpy (mnem, p);
	    
	    if (! (p = strtok (NULL, " \t(")) || strcmp (p, "CMDDEF") != 0)
		bailout ();

	    if (! (p = strtok (NULL, "( \t,")))
		bailout ();
	    opcode = strtol (p, &p, 0);
	    if (*p != '\0')
		bailout ();

	    if (! (p = strtok (NULL, " \t,")))
		bailout ();

	    if (strcmp (p, "TOK_CMD_DIRECT_PCREL") == 0) {
		if (opcode < 0 || opcode > 255)
		    bailout ();
		tab_pcrel[opcode] = 1;
	    }

	    if (strcmp (p, "TOK_CMD_DIRECT") == 0
		|| strcmp (p, "TOK_CMD_DIRECT_PCREL") == 0) {
		add_entry (&tab_direct, &ntab_direct, opcode, mnem);
	    }
	    else if (strcmp (p, "TOK_CMD_INDIR") == 0) {
		if (opcode < 0)
		    add_entry (&tab_indir_negative, &ntab_indir_negative,
			       -opcode, mnem);
		else
		    add_entry (&tab_indir, &ntab_indir, opcode, mnem);
	    }
	    else if (strcmp (p, "TOK_CMD_FPU") == 0) {
		add_entry (&tab_fpu, &ntab_fpu, opcode, mnem);
	    }
	    else {
		bailout ();
	    }
	}
    }

    printf ("/* This file is generated automatically by the mktabs tool.\n");
    printf ("   Do not edit.  */\n");
    printf ("\n\n\n");

    printf ("char *tab_direct[] = {\n");
    while (tab_direct[ntab_direct-1] == nullstr)
	ntab_direct--;
    for (opcode=0; opcode < ntab_direct; opcode++)
	printf ("\t%s,\t/* 0x%02x */\n", tab_direct[opcode], opcode);
    printf ("};\n\n");

    printf ("char tab_pcrel[] = {\n");
    for (opcode=0; opcode < ntab_direct; opcode++)
	printf ("\t%d,\t/* 0x%02x */\n", tab_pcrel[opcode], opcode);
    printf ("};\n\n");

    printf ("char *tab_indir[] = {\n");
    while (tab_indir[ntab_indir-1] == nullstr)
	ntab_indir--;
    for (opcode=0; opcode < ntab_indir; opcode++)
	printf ("\t%s,\t/* 0x%02x */\n", tab_indir[opcode], opcode);
    printf ("};\n\n");

    printf ("char *tab_indir_negative[] = {\n");
    while (tab_indir_negative[ntab_indir_negative-1] == nullstr)
	ntab_indir_negative--;
    for (opcode=0; opcode < ntab_indir_negative; opcode++)
	printf ("\t%s,\t/* -0x%02x */\n", tab_indir_negative[opcode], opcode);
    printf ("};\n\n");

    printf ("char *tab_fpu[] = {\n");
    while (tab_fpu[ntab_fpu-1] == nullstr)
	ntab_fpu--;
    for (opcode=0; opcode < ntab_fpu; opcode++)
	printf ("\t%s,\t/* 0x%02x */\n", tab_fpu[opcode], opcode);
    printf ("};\n\n");

    return 0;
}

void
bailout ()
{
    fprintf (stderr, "unexpected input at line %u\n", lineno);
    exit (1);
}

void
add_entry (char ***tabp, int *tabszp, int opcode, char *mnem)
{
    if (*tabszp <= opcode) {
	int newsz = opcode + 50;
	char **ppp;
	*tabp = xrealloc (*tabp, newsz * sizeof (**tabp));
	for (ppp = &(*tabp)[*tabszp]; ppp < &(*tabp)[newsz]; ppp++)
	    *ppp = nullstr;
	*tabszp = newsz;
    }

    (*tabp)[opcode] = mnem;
}

void *
xmalloc (size_t size)
{
    void *p = malloc (size);

    if (!p) {
	fprintf (stderr, "Memory exhausted -- that's the end.\n");
	exit (1);
    }

    return p;
}

void *
xrealloc (void *old, size_t size)
{
    void *p = realloc (old, size);

    if (!p) {
	fprintf (stderr, "Memory exhausted -- that's the end.\n");
	exit (1);
    }

    return p;
}
