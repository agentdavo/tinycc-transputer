/* $Id: disassemble.h,v 1.1.1.1 1995/12/22 12:24:39 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#ifndef DISAS_H
#define DISAS_H

/* Result of a call to disassemble () */

struct disassemble {
    char *mnem;		/* command's mnemonics */
    int hasarg;		/* flag: command with argument (direct) */
    int pcrel;		/* flag: the argument is pc-relative (j,cj,call) */
    long arg;		/* the argument, if any */
    int size;		/* size of the disassembled area */
    char dump[40];	/* hex dump string of the disassembled area */
};

extern char *tab_direct[];
extern char *tab_indir[];
extern char *tab_indir_negative[];
extern char *tab_fpu[];
extern char tab_pcrel[];

struct disassemble *disassemble (unsigned char *buf, int buflen);

#endif
