/* $Id: options.h,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/* When option definition lives close to usage instructions, the
   chances for them to be in sync are much better */

DEFINE_OPTION ("help", no_argument, NULL, 301, "\
--help			show the usage summary\n")

DEFINE_OPTION ("output", required_argument, NULL, 'o', "\
-o F, --output F	output to file F (defaults to stdout)\n")

DEFINE_OPTION ("expr-print-long", no_argument, NULL, 'E', "\
-E, --expr-print-long	longer flavour of expression dumps\n")

DEFINE_OPTION ("disassemble", required_argument, NULL, 'd', "\
-d S, --disassemble S	show CMD and BLOCK elements in the segment S\n\
			as machine instructions\n")
