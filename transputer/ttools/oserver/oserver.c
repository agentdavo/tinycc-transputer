/* $Id: oserver.c,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/* oserver (or rather 0-server) is tiny little program which boots the
   transputer on a BOO4 board from the file(s) supplied as arguments,
   and then cycles forever reading whatever transputer might output to
   the link, and translating that to stdout.  */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <link.h>


void reader (void);
void kill_child (void);
void kill_parent (void);
void graceful_exit (int sig);


char *boot_n;
char *out_n = "stdout";
char *link_n = "/dev/link0";
int link_fd;
pid_t child_pid;
int verbose = 0;
int hexdump = 0;
int noreset = 0;

static char *myname = "oserver";
extern char *version_string;

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
	fprintf (stderr, "This is oserver version %s\n", version_string);
	fprintf (stderr, "Usage: %s [options] BOOTFILE ...\n", myname);
	fprintf (stderr, "%s --help for options list\n", myname);
	break;

    case USAGE_OPTIONS:
	fprintf (stderr, "This is oserver version %s\n", version_string);
	fprintf (stderr, "Usage: %s [options] BOOTFILE [...]\n", myname);
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
    int c;

    /* Find out how we are named on this system */
    if ((myname = strrchr (argv[0], '/') + 1) == NULL + 1)
	myname = argv[0];

    while ((c = getopt_long (argc, argv, "ho:rv",
			     opttab, &dummy)) != -1) {
	switch (c) {
	case '?':		/* an illegal option */
	    usage ("", USAGE_GENERAL, 1);

	case OPTION_HELP:
	    usage ("", USAGE_OPTIONS, 0);

	case 'h':
	    hexdump = 1;
	    break;

	case 'o':
	    if (strcmp (optarg, "-") != 0) {
		out_n = optarg;
		if (! freopen (out_n, "wb", stdout)) {
		    perror (out_n);
		    exit (1);
		}
	    }
	    break;
		
	case 'r':
	    noreset = 1;
	    break;

	case 'v':
	    verbose = 1;
	    break;

	default:
	    abort ();
	}
    }

    /* If no bootfiles given, we won't work */
    if (optind >= argc)
	usage ("", USAGE_GENERAL, 1);

    /* Open the link device */
    if ((link_fd = open (link_n, O_RDWR)) < 0) {
	perror (link_n);
	exit (1);
    }

    /* Reset the transputer */
    if (! noreset) {
	if (ioctl(link_fd, LINKRESET, 1) < 0) {
	    perror ("cannot reset transputer"); 
	    exit (1);
	}
    }

    /* Set up link driver timeouts to 0.5 second so as not to block in
       link reads/writes */
    if (ioctl(link_fd, LINKREADTIMEOUT, verbose? 1: 50) < 0
	|| ioctl(link_fd, LINKREADABORT, 1) < 0 
	|| ioctl(link_fd, LINKWRITETIMEOUT, verbose? 1: 50) < 0
	|| ioctl(link_fd, LINKWRITEABORT, 1) < 0) {
	perror ("cannot set link timeouts"); 
	exit (1);
    }

    /* Make sure the second process exits too if we get a signal */
    signal(SIGHUP, graceful_exit);
    signal(SIGINT, graceful_exit);
    signal(SIGTERM, graceful_exit);

    /* Fork off a child for reading the link */
    switch (child_pid = fork ()) {
    case -1:
	perror ("fork");
	exit (1);
    case 0:
	atexit (kill_parent);
	reader ();
	exit (0);
    default:
	atexit (kill_child);
	break;
    }

    /* The parent goes on booting */

    /* Open bootfile. */
    for ( ; optind < argc; optind++) {
	char buf[BUFSIZ];
	long count = 0;
	int n;

	boot_n = argv[optind];
	if (strcmp (boot_n, "-") == 0)
	    boot_n = "stdin";
	else {
	    if (! freopen (boot_n, "rb", stdin)) {
		perror (boot_n);
		exit (1);
	    }
	}

	if (verbose)
	    fprintf (stderr, "%s:\n", boot_n);

	while ((n = fread (buf, 1, BUFSIZ, stdin)) > 0) {
	    int i = 0;

	    while (i < n) {
		int k;

		/* This write will time out after LINKWRITETIMEOUT jiffies */
		k = write (link_fd, &buf[i], n-i);
		if (k == -1) {
		    perror (link_n);
		    exit (1);
		}
		i += k;
		count += k;

		if (verbose && k)
		    fprintf (stderr, "%s: wrote %d bytes, total %ld\n",
			     boot_n, k, count);
	    }
	}
	if (verbose)
	    fprintf (stderr, "\n");
    }

    /* sleep until we get killed */
    for (;;)
	sleep (100000);
}

/* Read anything that transputer might write to the link, and dump
   that to stdout, until we are killed. */

void
reader ()
{
    char buf[BUFSIZ];

    for (;;) {
	int k;

	/* This read will time out after LINKREADTIMEOUT jiffies */
	k = read (link_fd, buf, BUFSIZ);
	if (k == -1) {
	    perror (link_n);
	    exit (1);
	}

	if (k) {
	    if (! hexdump)
		fwrite (buf, 1, k, stdout);
	    else {
		int i = 0;

		for (i=0; i < k; i++) {
		    printf ("%02x", (unsigned char) buf[i]);
		    if (i % 16 == 7)
			printf ("-");
		    else if (i % 16 == 15)
			printf ("\n");
		    else
			printf (" ");
		}
		printf ("\n");
	    }
	    fflush (stdout);
	    if (ferror (stdout)) {
		perror ("stdout");
		exit (1);
	    }
	}
    }

    /*NOTREACHED*/
}

void
kill_child ()
{
    kill (child_pid, SIGTERM);
}

void
kill_parent ()
{
    int ppid = getppid ();

    if (ppid != 1)
	kill (ppid, SIGTERM);
}

void
graceful_exit (int sig)
{
    /* Make functions registered with atexit work */
    exit (sig);
}
