/* $Id: cmd.h,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

#ifndef CMD_H
#define CMD_H

/* certain opcodes used here and there */
#define OPCODE_PFIX	(0x20)
#define OPCODE_NFIX	(0x60)
#define OPCODE_LDC	(0x40)
#define OPCODE_OPR	(0xf0)
#define OPCODE_FPENTRY	(0xab)


/* max number of bytes a command can occupy */
#define CMD_MAXLEN	(8)
#define WIDEST_SLOT	(CMD_MAXLEN)

typedef long int target_word_t;
typedef unsigned long int target_word_unsigned_t;

#ifndef NO_LIBC
int cmd_size (target_word_t arg);
int cmd_generate (char *buf, int opcode, target_word_t arg, int slot);
#endif


#endif /* not CMD_H */
