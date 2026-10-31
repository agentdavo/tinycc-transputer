# $Id: Makefile,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $

# This file is part of TTOOLS package.
# Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>

SHELL = /bin/sh
VPATH = ../shared:../dast

OBJS1 = litdump.o
OBJS2 = expr.o misc.o ments.o litin.o litout.o cmd.o
OBJS3 = version.o
OBJS4 = disassemble.o
OBJS = $(OBJS1) $(OBJS2) $(OBJS3) $(OBJS4)
LIBS =

CFLAGS_DEB=-g
CFLAGS_DEF=
CFLAGS_INC=-I../shared -I../dast
CFLAGS_ANY=-pipe
CFLAGS_OPT=-m486
CFLAGS_WRN=-Wall

CFLAGS=$(CFLAGS_INC) $(CFLAGS_OPT) $(CFLAGS_DEB) $(CFLAGS_WRN) \
       $(CFLAGS_DEF) $(CFLAGS_ANY)


all:  litdump

litdump:  $(OBJS)
	$(CC) $(LDFLAGS) -o litdump $(OBJS) $(LIBS)

dastabs.h: ../ast/scan.l
	cd ../dast; make dastabs.h

TAGS: force
	etags *.h ../shared/*.h  ../dast/disassemble.h \
	  *.c ../shared/*.c ../dast/disassemble.c

depend dep:
	for i in litdump.c ../dast/disassemble.c ../shared/*.c; do \
	  $(CC) $(CFLAGS) -MM $$i; done >> Depend
	sed '/^### Dep/q' Makefile | cat - Depend > Makefile.new
	rm Depend
	@if cmp Makefile.new Makefile; \
	then \
	  echo "** Dependencies unchanged"; \
	  rm Makefile.new; \
	else \
	  if [ -w Makefile ]; \
	  then mv Makefile.new Makefile; echo "** Dependencies changed"; \
	  else \
	    echo "** Please \`co -l Makefile' and rerun \`make depend'"; \
	    exit 1; \
	  fi; \
	fi

clean:
	rm -f *.o y.output a.out Depend

distclean: clean
	rm -f litdump mktabs TAGS

realclean: distclean

force:

### Dependencies start here -- do not change this line #######
litdump.o: litdump.c litdump.h ../shared/shared.h ../shared/config.h \
 ../shared/expr.h ../shared/misc.h ../shared/cmd.h ../shared/ments.h \
 ../shared/litio.h ../dast/disassemble.h options.h
disassemble.o: ../dast/disassemble.c ../shared/shared.h \
 ../shared/config.h ../shared/expr.h ../shared/misc.h ../shared/cmd.h \
 ../shared/ments.h ../shared/litio.h ../dast/disassemble.h \
 ../dast/dastabs.h
cmd.o: ../shared/cmd.c ../shared/shared.h ../shared/config.h \
 ../shared/expr.h ../shared/misc.h ../shared/cmd.h ../shared/ments.h \
 ../shared/litio.h
expr.o: ../shared/expr.c ../shared/shared.h ../shared/config.h \
 ../shared/expr.h ../shared/misc.h ../shared/cmd.h ../shared/ments.h \
 ../shared/litio.h
litin.o: ../shared/litin.c ../shared/shared.h ../shared/config.h \
 ../shared/expr.h ../shared/misc.h ../shared/cmd.h ../shared/ments.h \
 ../shared/litio.h
litout.o: ../shared/litout.c ../shared/shared.h ../shared/config.h \
 ../shared/expr.h ../shared/misc.h ../shared/cmd.h ../shared/ments.h \
 ../shared/litio.h
ments.o: ../shared/ments.c ../shared/shared.h ../shared/config.h \
 ../shared/expr.h ../shared/misc.h ../shared/cmd.h ../shared/ments.h \
 ../shared/litio.h
misc.o: ../shared/misc.c ../shared/shared.h ../shared/config.h \
 ../shared/expr.h ../shared/misc.h ../shared/cmd.h ../shared/ments.h \
 ../shared/litio.h
version.o: ../shared/version.c
