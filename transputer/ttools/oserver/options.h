/* $Id: options.h,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

/* When option definition lives close to usage instructions, the
   chances for them to be in sync are much better */

#define OPTION_HELP  301
DEFINE_OPTION ("help", no_argument, NULL, OPTION_HELP, "\
--help			show the usage summary\n")

DEFINE_OPTION ("hex", no_argument, NULL, 'h', "\
-h, --hex		write output data as hex dump instead of binary\n")

DEFINE_OPTION ("output", required_argument, NULL, 'o', "\
-o F, --output F	output to file F (defaults to stdout)\n")

DEFINE_OPTION ("noreset", no_argument, NULL, 'r', "\
-r, --noreset		do not reset transputer\n")

DEFINE_OPTION ("verbose", no_argument, NULL, 'v', "\
-v, --verbose		display link write counter to stderr\n")

