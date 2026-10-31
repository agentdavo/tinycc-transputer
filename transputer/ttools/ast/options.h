/* $Id: options.h,v 1.1.1.1 1995/12/22 12:24:38 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

/* When option definition lives close to usage instructions, the
   chances for them to be in sync are much better */

#define OPTION_HELP  301
DEFINE_OPTION ("help", no_argument, NULL, OPTION_HELP, "\
--help			show the usage summary\n")

DEFINE_OPTION ("output", required_argument, NULL, 'o', "\
-o F, --output F	output to file F (default a.out)\n")

DEFINE_OPTION ("expr-print-long", no_argument, NULL, 'E', "\
-E, --expr-print-long	longer flavour of expression dumps\n")

DEFINE_OPTION ("maxpasses", required_argument, NULL, 'p', "\
-p N, --maxpasses N	not more than N offset optimization passes (default 8)\n")

DEFINE_OPTION ("trace", required_argument, NULL, 't', "\
-t SPEC, --trace SPEC	enable debugging output\n")

DEFINE_OPTION ("dump", required_argument, NULL, 'd', "\
-d SPEC, --dump SPEC	enable debugging dumps\n")
