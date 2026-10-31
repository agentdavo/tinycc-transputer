/* $Id: scan.l,v 1.4 1996/01/16 16:05:45 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

%x STRING
%x COMMENT
%x LINEDIR

%{
#include "ast.h"
#include "y.tab.h"
#include <errno.h>

char *input_filename = "<UNKNOWN>";
int input_lineno = 1;

static char *string = NULL;
static int stringlen = 0;
static int stringlenmax = 0;

#define LEX_RETURN(token) \
  yylval.line = input_lineno; \
  yylval.file = input_filename;	\
  if (traceset & TRACE_LEX) \
    print_token (stdout, token, &yylval); \
  return (token)

#define CMDDEF(opcode, token, processor_currently_unused) \
  yylval.u.num = opcode; \
  LEX_RETURN (token);

static int c_char_escape (int c);
static long strntol (char *str, int leng, int base);
static void mk_string_space (int leng_requested);

%}
%%

 /* command mnemonics */

"j"	       	CMDDEF (0x00, TOK_CMD_DIRECT_PCREL, CORE)
"ldlp"		CMDDEF (0x10, TOK_CMD_DIRECT, CORE)
"pfix"		CMDDEF (0x20, TOK_CMD_DIRECT, CORE)
"ldnl"		CMDDEF (0x30, TOK_CMD_DIRECT, CORE)
"ldc"		CMDDEF (0x40, TOK_CMD_DIRECT, CORE)
"ldnlp"		CMDDEF (0x50, TOK_CMD_DIRECT, CORE)
"nfix"		CMDDEF (0x60, TOK_CMD_DIRECT, CORE)
"ldl"		CMDDEF (0x70, TOK_CMD_DIRECT, CORE)
"adc"		CMDDEF (0x80, TOK_CMD_DIRECT, CORE)
"call"		CMDDEF (0x90, TOK_CMD_DIRECT_PCREL, CORE)
"cj" 		CMDDEF (0xa0, TOK_CMD_DIRECT_PCREL, CORE)
"ajw"		CMDDEF (0xb0, TOK_CMD_DIRECT, CORE)
"eqc"		CMDDEF (0xc0, TOK_CMD_DIRECT, CORE)
"stl"		CMDDEF (0xd0, TOK_CMD_DIRECT, CORE)
"stnl"		CMDDEF (0xe0, TOK_CMD_DIRECT, CORE)
"opr"		CMDDEF (0xf0, TOK_CMD_DIRECT, CORE)

"rev"		CMDDEF (0x00, TOK_CMD_INDIR, CORE)
"lb" 		CMDDEF (0x01, TOK_CMD_INDIR, CORE)
"bsub"		CMDDEF (0x02, TOK_CMD_INDIR, CORE)
"endp"		CMDDEF (0x03, TOK_CMD_INDIR, CORE)
"diff"		CMDDEF (0x04, TOK_CMD_INDIR, CORE)
"add"		CMDDEF (0x05, TOK_CMD_INDIR, CORE)
"gcall"		CMDDEF (0x06, TOK_CMD_INDIR, CORE)
"in" 		CMDDEF (0x07, TOK_CMD_INDIR, CORE)
"prod"		CMDDEF (0x08, TOK_CMD_INDIR, CORE)
"gt" 		CMDDEF (0x09, TOK_CMD_INDIR, CORE)
"wsub"		CMDDEF (0x0a, TOK_CMD_INDIR, CORE)
"out"		CMDDEF (0x0b, TOK_CMD_INDIR, CORE)
"sub"		CMDDEF (0x0c, TOK_CMD_INDIR, CORE)
"startp"	CMDDEF (0x0d, TOK_CMD_INDIR, CORE)
"outbyte"	CMDDEF (0x0e, TOK_CMD_INDIR, CORE)
"outword"	CMDDEF (0x0f, TOK_CMD_INDIR, CORE)

"seterr"	CMDDEF (0x10, TOK_CMD_INDIR, CORE&~T9000)
"resetch"	CMDDEF (0x12, TOK_CMD_INDIR, CORE)
"csub0"		CMDDEF (0x13, TOK_CMD_INDIR, CORE)
"stopp"		CMDDEF (0x15, TOK_CMD_INDIR, CORE)
"ladd"	        CMDDEF (0x16, TOK_CMD_INDIR, CORE)
"stlb"	        CMDDEF (0x17, TOK_CMD_INDIR, CORE&~T9000)
"sthf"	        CMDDEF (0x18, TOK_CMD_INDIR, CORE&~T9000)
"norm"	        CMDDEF (0x19, TOK_CMD_INDIR, CORE)
"ldiv"	        CMDDEF (0x1a, TOK_CMD_INDIR, CORE)
"ldpi"	        CMDDEF (0x1b, TOK_CMD_INDIR, CORE)
"stlf"	        CMDDEF (0x1c, TOK_CMD_INDIR, CORE&~T9000)
"xdble"	        CMDDEF (0x1d, TOK_CMD_INDIR, CORE)
"ldpri"	        CMDDEF (0x1e, TOK_CMD_INDIR, CORE)
"rem"	        CMDDEF (0x1f, TOK_CMD_INDIR, CORE)
"ret"	        CMDDEF (0x20, TOK_CMD_INDIR, CORE)
"lend"	        CMDDEF (0x21, TOK_CMD_INDIR, CORE)
"ldtimer"	CMDDEF (0x22, TOK_CMD_INDIR, CORE)
"testerr"	CMDDEF (0x29, TOK_CMD_INDIR, CORE&~T9000)
"testpranal"	CMDDEF (0x2a, TOK_CMD_INDIR, CORE)
"tin"	        CMDDEF (0x2b, TOK_CMD_INDIR, CORE)
"div"	        CMDDEF (0x2c, TOK_CMD_INDIR, CORE)
"dist"		CMDDEF (0x2e, TOK_CMD_INDIR, CORE)
"disc"	        CMDDEF (0x2f, TOK_CMD_INDIR, CORE)
"diss"	        CMDDEF (0x30, TOK_CMD_INDIR, CORE)
"lmul"	        CMDDEF (0x31, TOK_CMD_INDIR, CORE)
"not"	        CMDDEF (0x32, TOK_CMD_INDIR, CORE)
"xor"	        CMDDEF (0x33, TOK_CMD_INDIR, CORE)
"bcnt"	        CMDDEF (0x34, TOK_CMD_INDIR, CORE)
"lshr"	        CMDDEF (0x35, TOK_CMD_INDIR, CORE)
"lshl"	        CMDDEF (0x36, TOK_CMD_INDIR, CORE)
"lsum"	        CMDDEF (0x37, TOK_CMD_INDIR, CORE)
"lsub"	        CMDDEF (0x38, TOK_CMD_INDIR, CORE)
"runp"	        CMDDEF (0x39, TOK_CMD_INDIR, CORE)
"xword"	        CMDDEF (0x3a, TOK_CMD_INDIR, CORE)
"sb"	        CMDDEF (0x3b, TOK_CMD_INDIR, CORE)
"gajw"	        CMDDEF (0x3c, TOK_CMD_INDIR, CORE)
"savel"	        CMDDEF (0x3d, TOK_CMD_INDIR, CORE&~T9000)
"saveh"	        CMDDEF (0x3e, TOK_CMD_INDIR, CORE&~T9000)
"wcnt"	        CMDDEF (0x3f, TOK_CMD_INDIR, CORE)
"shr"	        CMDDEF (0x40, TOK_CMD_INDIR, CORE)
"shl"	        CMDDEF (0x41, TOK_CMD_INDIR, CORE)
"mint"	        CMDDEF (0x42, TOK_CMD_INDIR, CORE)
"alt"	        CMDDEF (0x43, TOK_CMD_INDIR, CORE)
"altwt"		CMDDEF (0x44, TOK_CMD_INDIR, CORE)
"altend"	CMDDEF (0x45, TOK_CMD_INDIR, CORE)
"and"		CMDDEF (0x46, TOK_CMD_INDIR, CORE)
"enbt"		CMDDEF (0x47, TOK_CMD_INDIR, CORE)
"enbc"		CMDDEF (0x48, TOK_CMD_INDIR, CORE)
"enbs"		CMDDEF (0x49, TOK_CMD_INDIR, CORE)
"move"		CMDDEF (0x4a, TOK_CMD_INDIR, CORE)
"or"		CMDDEF (0x4b, TOK_CMD_INDIR, CORE)
"csngl"		CMDDEF (0x4c, TOK_CMD_INDIR, CORE)
"ccnt1"		CMDDEF (0x4d, TOK_CMD_INDIR, CORE)
"talt"		CMDDEF (0x4e, TOK_CMD_INDIR, CORE)
"ldiff"		CMDDEF (0x4f, TOK_CMD_INDIR, CORE)
"sthb"		CMDDEF (0x50, TOK_CMD_INDIR, CORE&~T9000)
"taltwt"	CMDDEF (0x51, TOK_CMD_INDIR, CORE)
"sum"		CMDDEF (0x52, TOK_CMD_INDIR, CORE)
"mul"		CMDDEF (0x53, TOK_CMD_INDIR, CORE)
"sttimer"	CMDDEF (0x54, TOK_CMD_INDIR, CORE)
"stoperr"	CMDDEF (0x55, TOK_CMD_INDIR, CORE&~T9000)
"cword"		CMDDEF (0x56, TOK_CMD_INDIR, CORE)
"clrhalterr"	CMDDEF (0x57, TOK_CMD_INDIR, CORE&~T9000)
"sethalterr"	CMDDEF (0x58, TOK_CMD_INDIR, CORE&~T9000)
"testhalterr"	CMDDEF (0x59, TOK_CMD_INDIR, CORE&~T9000)
"dup"		CMDDEF (0x5a, TOK_CMD_INDIR, DUP)
"move2dinit"	CMDDEF (0x5b, TOK_CMD_INDIR, MOVE2D)
"move2dall"	CMDDEF (0x5c, TOK_CMD_INDIR, MOVE2D)
"move2dnonzero"	CMDDEF (0x5d, TOK_CMD_INDIR, MOVE2D)
"move2dzero"	CMDDEF (0x5e, TOK_CMD_INDIR, MOVE2D)
"gtu"		CMDDEF (0x5f, TOK_CMD_INDIR, T9000|T450)
"unpacksn"	CMDDEF (0x63, TOK_CMD_INDIR, FP_SUPPORT)
"slmul"		CMDDEF (0x64, TOK_CMD_INDIR, T450)
"sulmul"	CMDDEF (0x65, TOK_CMD_INDIR, T450)
"satadd"	CMDDEF (0x68, TOK_CMD_INDIR, T450)
"satsub"	CMDDEF (0x69, TOK_CMD_INDIR, T450)
"satmul"	CMDDEF (0x6a, TOK_CMD_INDIR, T450)
"postnormsn"	CMDDEF (0x6c, TOK_CMD_INDIR, FP_SUPPORT)
"roundsn"	CMDDEF (0x6d, TOK_CMD_INDIR, FP_SUPPORT)
"ldtraph"	CMDDEF (0x6e, TOK_CMD_INDIR, T450)
"ldinf"		CMDDEF (0x71, TOK_CMD_INDIR, FP_SUPPORT)
"fmul"		CMDDEF (0x72, TOK_CMD_INDIR, FMUL)
"cflerr"	CMDDEF (0x73, TOK_CMD_INDIR, FP_SUPPORT)
"crcword"	CMDDEF (0x74, TOK_CMD_INDIR, CRC)
"crcbyte"	CMDDEF (0x75, TOK_CMD_INDIR, CRC)
"bitcnt"	CMDDEF (0x76, TOK_CMD_INDIR, BITOPS)
"bitrevword"	CMDDEF (0x77, TOK_CMD_INDIR, BITOPS)
"bitrevnbits"	CMDDEF (0x78, TOK_CMD_INDIR, BITOPS)
"pop"		CMDDEF (0x79, TOK_CMD_INDIR, T805|T9000|T450)
"ldmemstartval"	CMDDEF (0x7e, TOK_CMD_INDIR, T9000)
"wsubdb"	CMDDEF (0x81, TOK_CMD_INDIR, WSUBDB)
"fpldnldbi"	CMDDEF (0x82, TOK_CMD_INDIR, FPU_CORE)
"fpchkerr"	CMDDEF (0x83, TOK_CMD_INDIR, FPU_CORE&~T9000)
"fpstnldb"	CMDDEF (0x84, TOK_CMD_INDIR, FPU_CORE)
"fpldnlsni"	CMDDEF (0x86, TOK_CMD_INDIR, FPU_CORE)
"fpadd"		CMDDEF (0x87, TOK_CMD_INDIR, FPU_CORE)
"fpstnlsn"	CMDDEF (0x88, TOK_CMD_INDIR, FPU_CORE)
"fpsub"		CMDDEF (0x89, TOK_CMD_INDIR, FPU_CORE)
"fpldnldb"	CMDDEF (0x8a, TOK_CMD_INDIR, FPU_CORE)
"fpmul"		CMDDEF (0x8b, TOK_CMD_INDIR, FPU_CORE)
"fpdiv"		CMDDEF (0x8c, TOK_CMD_INDIR, FPU_CORE)
"fprange"	CMDDEF (0x8d, TOK_CMD_INDIR, T9000)
"fpldnlsn"	CMDDEF (0x8e, TOK_CMD_INDIR, FPU_CORE)
"fpremfirst"	CMDDEF (0x8f, TOK_CMD_INDIR, FPU_CORE&~T9000)
"fpremstep"	CMDDEF (0x90, TOK_CMD_INDIR, FPU_CORE&~T9000)
"fpnan"		CMDDEF (0x91, TOK_CMD_INDIR, FPU_CORE)
"fpordered"	CMDDEF (0x92, TOK_CMD_INDIR, FPU_CORE)
"fpnotfinite"	CMDDEF (0x93, TOK_CMD_INDIR, FPU_CORE)
"fpgt"		CMDDEF (0x94, TOK_CMD_INDIR, FPU_CORE)
"fpeq"		CMDDEF (0x95, TOK_CMD_INDIR, FPU_CORE)
"fpi32tor32"	CMDDEF (0x96, TOK_CMD_INDIR, FPU_CORE)
"fpge"		CMDDEF (0x97, TOK_CMD_INDIR, T9000)
"fpi32tor64"	CMDDEF (0x98, TOK_CMD_INDIR, FPU_CORE)
"fpb32tor64"	CMDDEF (0x9a, TOK_CMD_INDIR, FPU_CORE)
"fplg"		CMDDEF (0x9b, TOK_CMD_INDIR, T9000)
"fptesterr"	CMDDEF (0x9c, TOK_CMD_INDIR, FPU_CORE&~T9000)
"fprtoi32"	CMDDEF (0x9d, TOK_CMD_INDIR, FPU_CORE)
"fpstnli32"	CMDDEF (0x9e, TOK_CMD_INDIR, FPU_CORE)
"fpldzerosn"	CMDDEF (0x9f, TOK_CMD_INDIR, FPU_CORE)
"fpldzerodb"	CMDDEF (0xa0, TOK_CMD_INDIR, FPU_CORE)
"fpint"		CMDDEF (0xa1, TOK_CMD_INDIR, FPU_CORE)
"fpdup"		CMDDEF (0xa3, TOK_CMD_INDIR, FPU_CORE)
"fprev"		CMDDEF (0xa4, TOK_CMD_INDIR, FPU_CORE)
"fpldnladddb"	CMDDEF (0xa6, TOK_CMD_INDIR, FPU_CORE)
"fpldnlmuldb"	CMDDEF (0xa8, TOK_CMD_INDIR, FPU_CORE)
"fpldnladdsn"	CMDDEF (0xaa, TOK_CMD_INDIR, FPU_CORE)
"fpentry"	CMDDEF (0xab, TOK_CMD_INDIR, CORE&~T9000)
"fpldnlmulsn"	CMDDEF (0xac, TOK_CMD_INDIR, FPU_CORE)

"settimeslice"	CMDDEF (0xb0, TOK_CMD_INDIR, T9000|T450)
"ldflags"	CMDDEF (0xb6, TOK_CMD_INDIR, T9000)
"stflags"	CMDDEF (0xb7, TOK_CMD_INDIR, T9000)
"xbword"	CMDDEF (0xb8, TOK_CMD_INDIR, T9000|T450)
"lbx"		CMDDEF (0xb9, TOK_CMD_INDIR, T9000|T450)
"cb"		CMDDEF (0xba, TOK_CMD_INDIR, T9000|T450)
"cbu"		CMDDEF (0xbb, TOK_CMD_INDIR, T9000|T450)
"insphdr"	CMDDEF (0xbc, TOK_CMD_INDIR, T9000)
"readbfr"	CMDDEF (0xbd, TOK_CMD_INDIR, T9000)
"ldconf"	CMDDEF (0xbe, TOK_CMD_INDIR, T9000)
"stconf"	CMDDEF (0xbf, TOK_CMD_INDIR, T9000)
"ldcnt"		CMDDEF (0xc0, TOK_CMD_INDIR, T9000)
"ssub"		CMDDEF (0xc1, TOK_CMD_INDIR, T9000|T450)
"ldth"		CMDDEF (0xc2, TOK_CMD_INDIR, T9000)
"ldchstatus"	CMDDEF (0xc3, TOK_CMD_INDIR, T9000)
"intdis"	CMDDEF (0xc4, TOK_CMD_INDIR, T9000|T450)
"intenb"	CMDDEF (0xc5, TOK_CMD_INDIR, T9000|T450)
"ldtrapped"	CMDDEF (0xc6, TOK_CMD_INDIR, T450)
"cir"		CMDDEF (0xc7, TOK_CMD_INDIR, T9000|T450)
"ss"		CMDDEF (0xc8, TOK_CMD_INDIR, T9000|T450)
"chantype"	CMDDEF (0xc9, TOK_CMD_INDIR, T9000)
"ls"		CMDDEF (0xca, TOK_CMD_INDIR, T9000|T450)
"sttrapped"	CMDDEF (0xcb, TOK_CMD_INDIR, T450)
"ciru"		CMDDEF (0xcc, TOK_CMD_INDIR, T9000|T450)
"gintdis"	CMDDEF (0xcd, TOK_CMD_INDIR, T450)
"gintenb"	CMDDEF (0xce, TOK_CMD_INDIR, T450)
"fprem"		CMDDEF (0xcf, TOK_CMD_INDIR, T9000)
"fprn"		CMDDEF (0xd0, TOK_CMD_INDIR, T9000)
"fpdivby2"	CMDDEF (0xd1, TOK_CMD_INDIR, T9000)
"fpmulby2"	CMDDEF (0xd2, TOK_CMD_INDIR, T9000)
"fpsqrt"	CMDDEF (0xd3, TOK_CMD_INDIR, T9000)
"fprp"		CMDDEF (0xd4, TOK_CMD_INDIR, T9000)
"fprm"		CMDDEF (0xd5, TOK_CMD_INDIR, T9000)
"fprz"		CMDDEF (0xd6, TOK_CMD_INDIR, T9000)
"fpr32tor64"	CMDDEF (0xd7, TOK_CMD_INDIR, T9000)
"fpr64tor32"	CMDDEF (0xd8, TOK_CMD_INDIR, T9000)
"fpexpdec32"	CMDDEF (0xd9, TOK_CMD_INDIR, T9000)
"fpexpinc32"	CMDDEF (0xda, TOK_CMD_INDIR, T9000)
"fpabs"		CMDDEF (0xdb, TOK_CMD_INDIR, T9000)
"fpchki32"	CMDDEF (0xde, TOK_CMD_INDIR, T9000)
"fpchki64"	CMDDEF (0xdf, TOK_CMD_INDIR, T9000)
"devlb"		CMDDEF (0xf0, TOK_CMD_INDIR, T9000|T450)
"devsb"		CMDDEF (0xf1, TOK_CMD_INDIR, T9000|T450)
"devls"		CMDDEF (0xf2, TOK_CMD_INDIR, T9000|T450)
"devss"		CMDDEF (0xf3, TOK_CMD_INDIR, T9000|T450)
"devlw"		CMDDEF (0xf4, TOK_CMD_INDIR, T9000|T450)
"devsw"		CMDDEF (0xf5, TOK_CMD_INDIR, T9000|T450)
"xsword"	CMDDEF (0xf8, TOK_CMD_INDIR, T9000|T450)
"lsx"		CMDDEF (0xf9, TOK_CMD_INDIR, T9000|T450)
"cs"		CMDDEF (0xfa, TOK_CMD_INDIR, T9000|T450)
"csu"		CMDDEF (0xfb, TOK_CMD_INDIR, T9000|T450)
"lddevid"	CMDDEF (0x17c, TOK_CMD_INDIR, LDDEVID)

"fpstall"	CMDDEF (-0x01, TOK_CMD_INDIR, T9000)
"fpldall"	CMDDEF (-0x02, TOK_CMD_INDIR, T9000)
"stshadow"	CMDDEF (-0x03, TOK_CMD_INDIR, T9000|T450)
"ldshadow"	CMDDEF (-0x04, TOK_CMD_INDIR, T9000|T450)
"tret"		CMDDEF (-0x05, TOK_CMD_INDIR, T9000|T450)
"goprot"	CMDDEF (-0x06, TOK_CMD_INDIR, T9000)
"selth"		CMDDEF (-0x07, TOK_CMD_INDIR, T9000)
"syscall"	CMDDEF (-0x08, TOK_CMD_INDIR, T9000)
"trapenb"	CMDDEF (-0x09, TOK_CMD_INDIR, T450)
"trapdis"	CMDDEF (-0x0a, TOK_CMD_INDIR, T450)
"wait"		CMDDEF (-0x0b, TOK_CMD_INDIR, T9000|T450)
"signal"	CMDDEF (-0x0c, TOK_CMD_INDIR, T9000|T450)
"timeslice"	CMDDEF (-0x0d, TOK_CMD_INDIR, T9000|T450)
"insertqueue"	CMDDEF (-0x0e, TOK_CMD_INDIR, T9000|T450)
"swaptimer"	CMDDEF (-0x0f, TOK_CMD_INDIR, T9000|T450)
"swapqueue"	CMDDEF (-0x10, TOK_CMD_INDIR, T9000|T450)
"iret"		CMDDEF (-0x11, TOK_CMD_INDIR, T450)
"stopch"	CMDDEF (-0x12, TOK_CMD_INDIR, T9000)
"vout"		CMDDEF (-0x13, TOK_CMD_INDIR, T9000)
"vin"		CMDDEF (-0x14, TOK_CMD_INDIR, T9000)
"swapbfr"	CMDDEF (-0x17, TOK_CMD_INDIR, T9000)
"sethdr"	CMDDEF (-0x18, TOK_CMD_INDIR, T9000)
"setchmode"	CMDDEF (-0x19, TOK_CMD_INDIR, T9000)
"initvlcb"	CMDDEF (-0x1a, TOK_CMD_INDIR, T9000)
"writehdr"	CMDDEF (-0x1b, TOK_CMD_INDIR, T9000)
"readhdr"	CMDDEF (-0x1c, TOK_CMD_INDIR, T9000)
"disg"		CMDDEF (-0x1d, TOK_CMD_INDIR, T9000)
"enbg"	        CMDDEF (-0x1e, TOK_CMD_INDIR, T9000)
"grant"	        CMDDEF (-0x1f, TOK_CMD_INDIR, T9000)
"stmove2dinit"	CMDDEF (-0x20, TOK_CMD_INDIR, T9000)
"causeerror"	CMDDEF (-0x21, TOK_CMD_INDIR, T9000)
"restart"	CMDDEF (-0x22, TOK_CMD_INDIR, T450)
"unmkrc"	CMDDEF (-0x23, TOK_CMD_INDIR, T9000)
"mkrc"	        CMDDEF (-0x24, TOK_CMD_INDIR, T9000)
"irdsq"	        CMDDEF (-0x25, TOK_CMD_INDIR, T9000)
"erdsq"	        CMDDEF (-0x26, TOK_CMD_INDIR, T9000)
"stresptr"	CMDDEF (-0x27, TOK_CMD_INDIR, T9000)
"ldresptr"	CMDDEF (-0x28, TOK_CMD_INDIR, T9000)
"devmove"	CMDDEF (-0x2c, TOK_CMD_INDIR, T9000|T450)
"icl"	        CMDDEF (-0x2d, TOK_CMD_INDIR, T9000)
"fdcl"	        CMDDEF (-0x2e, TOK_CMD_INDIR, T9000)
"ica"	        CMDDEF (-0x2f, TOK_CMD_INDIR, T9000)
"fdca"	        CMDDEF (-0x30, TOK_CMD_INDIR, T9000)
"nop"	        CMDDEF (-0x40, TOK_CMD_INDIR, T9000|T450)
"clockenb"	CMDDEF (-0x41, TOK_CMD_INDIR, T450)
"clockdis"	CMDDEF (-0x42, TOK_CMD_INDIR, T450)
"ldclock"	CMDDEF (-0x43, TOK_CMD_INDIR, T450)
"stclock"	CMDDEF (-0x44, TOK_CMD_INDIR, T450)
"reboot"	CMDDEF (-0x83, TOK_CMD_INDIR, T450)
"ldprodid"	CMDDEF (-0x84, TOK_CMD_INDIR, T9000|T450)

"fpusqrtfirst"	CMDDEF (0x01, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpusqrtstep"	CMDDEF (0x02, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpusqrtlast"	CMDDEF (0x03, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpurp"		CMDDEF (0x04, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpurm"		CMDDEF (0x05, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpurz"		CMDDEF (0x06, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpur32tor64"	CMDDEF (0x07, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpur64tor32"	CMDDEF (0x08, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpuexpdec32"	CMDDEF (0x09, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpuexpinc32"	CMDDEF (0x0a, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpuabs"	CMDDEF (0x0b, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpunoround"	CMDDEF (0x0d, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpuchki32"	CMDDEF (0x0e, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpuchki64"	CMDDEF (0x0f, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpudivby2"	CMDDEF (0x11, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpumulby2"	CMDDEF (0x12, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpurn"		CMDDEF (0x22, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpuseterr"	CMDDEF (0x23, TOK_CMD_FPU, FPU_CORE&~T9000)
"fpuclrerr"	CMDDEF (0x9c, TOK_CMD_FPU, FPU_CORE&~T9000)

 /* digital labels */

[0-9]/[ \t]*: {
    yylval.u.num = yytext[0] - '0';
    LEX_RETURN (TOK_DIGLAB);
}

[0-9]b {
    yylval.u.num = yytext[0] - '0';
    LEX_RETURN (TOK_DIGREF_B);
}
[0-9]f {
    yylval.u.num = yytext[0] - '0';
    LEX_RETURN (TOK_DIGREF_F);
}

 /* numbers of various formats */

0[xX][0-9a-fA-F]+ {
    yylval.u.lnum = strtoul (yytext, NULL, 16);
    if (yylval.u.lnum == ULONG_MAX && errno == ERANGE)
        error ("constant `%s' out of range\n", yytext);
    LEX_RETURN (TOK_NUMBER);
}
0[0-7]+ {
    yylval.u.lnum = strtoul (yytext, NULL, 8);
    if (yylval.u.lnum == ULONG_MAX && errno == ERANGE)
        error ("constant `%s' out of range\n", yytext);
    LEX_RETURN (TOK_NUMBER);
}
[0-9]+ {
    yylval.u.lnum = strtoul (yytext, NULL, 10);
    if (yylval.u.lnum == ULONG_MAX && errno == ERANGE)
        error ("constant `%s' out of range\n", yytext);
    LEX_RETURN (TOK_NUMBER);
}

 /* character constants */

'\\[xX][0-9a-fA-F]{2}' {
    yylval.u.lnum = strtoul (&yytext[3], NULL, 16);
    LEX_RETURN (TOK_NUMBER);
}
'\\[0-7]{1,3}' {
    yylval.u.lnum = strtoul (&yytext[2], NULL, 8);
    LEX_RETURN (TOK_NUMBER);
}
'\\.' {
    yylval.u.lnum = c_char_escape ((unsigned char) yytext[2]);
    LEX_RETURN (TOK_NUMBER);
}
'.' {
    yylval.u.lnum = ((unsigned char *) yytext)[1];
    LEX_RETURN (TOK_NUMBER);
}

 /* strings */

\" {
    BEGIN(STRING);
}
<STRING>\" {

    /* It would be safer to copy the string into a newly malloced
       region and have the caller freeing it.  However, with the
       current grammar the string contents is (hopefully) always used
       before asking lex about another token, so there is no real need
       to do the copying. */

    yylval.u.str.ptr = string;
    yylval.u.str.len = stringlen;
    stringlen = 0;
    BEGIN (INITIAL);
    LEX_RETURN (TOK_STRING);
}
<STRING>[^\\\"]+ {
    mk_string_space (yyleng);
    memcpy (&string[stringlen], yytext, yyleng);
    stringlen += yyleng;
}
<STRING>\\[0-7]{1,3} {
    mk_string_space (1);
    string[stringlen++] = strntol (&yytext[1], yyleng - 1, 8);
}
<STRING>\\[xX][0-9a-fA-F]{2} {
    mk_string_space (1);
    string[stringlen++] = strntol (&yytext[2], yyleng - 2, 16);
}
<STRING>\\. {
    mk_string_space (1);
    string[stringlen++] = c_char_escape ((unsigned char) yytext[1]);
}
<STRING><<EOF>> {
    error ("EOF in character constant\n");
}

 /* whitespace */

<INITIAL,COMMENT>[ \t\f\r]*\n[ \t\f\r]*	input_lineno++;
<INITIAL,COMMENT>[ \t\f\r]*

 /* C++-style comments */

"//".*	{
    while (input () != '\n')
	;
    unput ('\n');
}

 /* C-style comments: along the line of flexdoc(1) */

"/*" 			BEGIN (COMMENT);
<COMMENT>[^*\n]*        /* eat anything that's not a '*' */
<COMMENT>"*"+[^*/\n]*   /* eat up '*'s not followed by '/'s */
<COMMENT>"*"+"/"        BEGIN (INITIAL);

 /* #line and # directives */

^#/[ \t]*line[ \t]+[0-9]+[ \t]+\".+\"[ \t\r]*\n		BEGIN (LINEDIR);
^#/[ \t]*line[ \t]+[0-9]+[ \t]*\n			BEGIN (LINEDIR);
^#/[ \t]*[0-9]+[ \t]+\".+\"[ \t]+[0-9]+[ \t\r]*\n	BEGIN (LINEDIR);
^#/[ \t]*[0-9]+[ \t]+\".+\"[ \t\r]*\n			BEGIN (LINEDIR);
^#/[ \t]*[0-9]+[ \t\r]*\n				BEGIN (LINEDIR);
^#. {
    warning ("unrecognized preprocessor directive ignored\n");
    while (input () != '\n')
	;
    unput ('\n');	/* when ignoring, we want the newline counted */
    BEGIN (INITIAL);
}
<LINEDIR>[ \t]*		/* eat */
<LINEDIR>line		/* eat */
<LINEDIR>[0-9]+[ \t]+ {
    input_lineno = strtol (yytext, NULL, 10);
    if (input_lineno == -1 && errno == ERANGE)
        error ("constant `%s' out of range\n", yytext);
}
<LINEDIR>\r*\n		BEGIN (INITIAL);
<LINEDIR>\".+\" {

    /* The problem here is that we cannot overwrite the old filename,
       because it may still be referenced by one of the tokens
       currently in the yacc stack.  On the other hand, mallocing new
       space for every #line seen is terribly wasteful.  Our way out
       of this is to use hashtable designed for program symbols to
       keep filenames.  Only `ident' field is used in names of this
       fake namespace.  */

    input_filename = (table_lookup (&file_names, &yytext[1], yyleng-2))->ident;
    while (input () != '\n')	/* should gobble eol, we just set linino */
	;
    BEGIN (INITIAL);
}

 /* directives */

".word"		LEX_RETURN (TOK_WORD);
".half"		LEX_RETURN (TOK_HALF);
".byte"		LEX_RETURN (TOK_BYTE);
".ascii"	LEX_RETURN (TOK_BYTE);  /* allows a shortcut in GCC */
".string"	LEX_RETURN (TOK_STRING_DIR);  /* null-terminated string */
".align"	LEX_RETURN (TOK_ALIGN);
".globl"	LEX_RETURN (TOK_GLOBL);
".comm" 	LEX_RETURN (TOK_COMM);
".need"		LEX_RETURN (TOK_NEED);
".slot"		LEX_RETURN (TOK_SLOT);

 /* any other word started with a dot is considered a segment directive */

"."[a-zA-Z0-9_$@]+ {
    yylval.u.name = table_lookup (&segment_names, &yytext[1], yyleng-1);
    LEX_RETURN (TOK_SEGMENT);
}

 /* operators and other short character sequences */

"?"		LEX_RETURN ('?');
"["		LEX_RETURN ('[');
"]"		LEX_RETURN (']');
"("		LEX_RETURN ('(');
")"		LEX_RETURN (')');
"||"		LEX_RETURN (TOK_OROR);
"&&"		LEX_RETURN (TOK_ANDAND);
"~"		LEX_RETURN ('~');
"|"		LEX_RETURN ('|');
"^"		LEX_RETURN ('^');
"&"		LEX_RETURN ('&');
"="		LEX_RETURN ('=');
"=="		LEX_RETURN (TOK_EQ);
"!="		LEX_RETURN (TOK_NE);
"<"		LEX_RETURN ('<');
">"		LEX_RETURN ('>');
"<="		LEX_RETURN (TOK_LE);
">="		LEX_RETURN (TOK_GE);
"<<"		LEX_RETURN (TOK_LSH);
">>"		LEX_RETURN (TOK_RSH);
"+"		LEX_RETURN ('+');
"-"		LEX_RETURN ('-');
"*"		LEX_RETURN ('*');
"/"		LEX_RETURN ('/');
"%"		LEX_RETURN ('%');
";"		LEX_RETURN (';');
","		LEX_RETURN (',');
"."		LEX_RETURN ('.');
":"		LEX_RETURN (':');
"::"		LEX_RETURN (TOK_DCOLON);

 /* Identifiers.  Dots and dollars are allowed.  This comes in the end
    to avoid command mnemonics and directives being taken as identifiers. */

[a-zA-Z_$@][0-9a-zA-Z_.$@]* {
    yylval.u.name = table_lookup (&general_names, yytext, yyleng);
    LEX_RETURN (TOK_NAME);
}

.	error ("unexpected character '%c'\n", yytext[0]);


%%

/* Handle C-style character escape sequences: return 010 for 'b' and
   so on. */

static int
c_char_escape (int c)
{
    switch (c) {
    case '"':	return '"';
    case '\'':	return '\'';
    case '\\':	return '\\';
    case '0':	return '\0';
    case 'b':	return '\b';
    case 'f':	return '\f';
    case 'n':	return '\n';
    case 'r':	return '\r';
    case 't':	return '\t';
    case 'v':	return '\v';
    default:
	warning ("unknown escape sequence '\\%c' handled as mere '%c'\n", c, c);
	return c;
    }
}

/* A permutation of strtol: operates on non-zero-terminated strings */

static long
strntol (char *str, int leng, int base)
{
    char s[20];

    assert (leng < 20 - 1);

    memcpy (s, str, leng);
    s[leng] = '\0';

    return strtol (s, NULL, base);
}

/* Grow memory allocated for `string' if necessary */

static void
mk_string_space (int leng_requested)
{
    if (stringlenmax - stringlen < leng_requested) {
	stringlenmax = stringlenmax? stringlenmax*2: 80;
	string = (char *) xrealloc (string, stringlenmax);
    }
}
