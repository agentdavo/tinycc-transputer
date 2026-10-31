# $Id: Makefile,v 1.1.1.1 1995/12/22 12:24:38 sizif Exp $

# This file is part of TTOOLS package.
# Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>

SHELL = /bin/sh
VPATH = .:../shared

OBJS1 = parse.o scan.o ast.o
OBJS2 = cmd.o expr.o misc.o ments.o litout.o
OBJS3 = version.o
OBJS = $(OBJS1) $(OBJS2) $(OBJS3)
LIBS = -lfl

LFLAGS = -Ce
YFLAGS = -d -v

CFLAGS_DEB=-g -DEXPR_DEBUG
CFLAGS_DEF=-DAST
CFLAGS_INC=-I../shared
CFLAGS_ANY=-pipe
CFLAGS_OPT=
CFLAGS_WRN=-Wall

CFLAGS=$(CFLAGS_INC) $(CFLAGS_OPT) $(CFLAGS_DEB) $(CFLAGS_WRN) \
       $(CFLAGS_DEF) $(CFLAGS_ANY)


all:  ast

ast:  $(OBJS)
	$(CC) $(LDFLAGS) -o ast $(OBJS) $(LIBS)

TAGS: force
	mkdir temp;                                                     \
	mv -f parse.c scan.c temp;                                      \
	etags *.y *.l *.h ../shared/*.h  *.c ../shared/*.c;             \
	mv temp/* .;                                                    \
	rmdir temp

depend dep: parse.c scan.c
	for i in *.c; do $(CC) $(CFLAGS) -MM $$i; done > Depend
	for i in ../shared/*.c; do $(CC) $(CFLAGS) -MM $$i; done >> Depend
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
	rm -f *.o y.output a.out Depend Makefile.new

distclean: clean
	rm -f ast TAGS

realclean: distclean
	rm -f parse.c scan.c y.tab.h

force:

### Dependencies start here -- do not change this line #######
ast.o: ast.c ast.h ../shared/shared.h ../shared/config.h \
 ../shared/expr.h ../shared/misc.h ../shared/cmd.h ../shared/ments.h \
 ../shared/litio.h options.h
parse.o: parse.c ast.h ../shared/shared.h ../shared/config.h \
 ../shared/expr.h ../shared/misc.h ../shared/cmd.h ../shared/ments.h \
 ../shared/litio.h y.tab.h
scan.o: scan.c ast.h ../shared/shared.h ../shared/config.h \
 ../shared/expr.h ../shared/misc.h ../shared/cmd.h ../shared/ments.h \
 ../shared/litio.h y.tab.h
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
