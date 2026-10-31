/* $Id: options.h,v 1.1.1.1 1995/12/22 12:24:39 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

/* When option definition lives close to usage instructions, the
   chances for them to be in sync are much better */

DEFINE_OPTION ("help", no_argument, NULL, 301, "\
--help			show the usage summary\n")

DEFINE_OPTION ("output", required_argument, NULL, 'o', "\
-o F, --output F	output to file F (defaults to stdout)\n")

DEFINE_OPTION ("skip", required_argument, NULL, 's', "\
-s N, --skip N		skip first N bytes of the input file\n")

DEFINE_OPTION ("origin", required_argument, NULL, 'O', "\
-O A, --origin A	set memory address of the first disassembled cmd\n")

