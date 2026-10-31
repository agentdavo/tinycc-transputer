# $Id: Makefile,v 1.1.1.1 1995/12/22 12:24:39 sizif Exp $

# This file is part of TTOOLS package.
# Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>

SHELL = /bin/sh
VPATH = ../shared

OBJS1 = dast.o disassemble.o
OBJS2 = version.o
OBJS = $(OBJS1) $(OBJS2)
LIBS =

CFLAGS_DEB=-g
CFLAGS_DEF=
CFLAGS_INC=-I../shared
CFLAGS_ANY=-pipe
CFLAGS_OPT=-m486
CFLAGS_WRN=-Wall

CFLAGS=$(CFLAGS_INC) $(CFLAGS_OPT) $(CFLAGS_DEB) $(CFLAGS_WRN) \
       $(CFLAGS_DEF) $(CFLAGS_ANY)


all:  dast

dast:  $(OBJS)
	$(CC) $(LDFLAGS) -o dast $(OBJS) $(LIBS)

dastabs.h: ../ast/scan.l
	make mktabs
	./mktabs < ../ast/scan.l > dastabs.h

TAGS: force
	etags *.h *.c

depend dep:
	for i in *.c; do \
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
	rm -f dast mktabs TAGS

realclean: distclean

force:

### Dependencies start here -- do not change this line #######
dast.o: dast.c ../shared/shared.h ../shared/config.h ../shared/expr.h \
 ../shared/misc.h ../shared/cmd.h ../shared/ments.h ../shared/litio.h \
 disassemble.h options.h
disassemble.o: disassemble.c ../shared/shared.h ../shared/config.h \
 ../shared/expr.h ../shared/misc.h ../shared/cmd.h ../shared/ments.h \
 ../shared/litio.h disassemble.h dastabs.h
mktabs.o: mktabs.c
