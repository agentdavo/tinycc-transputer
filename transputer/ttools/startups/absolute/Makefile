# $Id: Makefile,v 1.1.1.1 1995/12/22 12:24:40 sizif Exp $

SHELL = /bin/sh
VPATH = .:../shared

OBJS1 = crossload.o
OBJS2 = litload.o
OBJS = $(OBJS1) $(OBJS2)
LIBS =

CFLAGS_DEB=-g
CFLAGS_DEF=
CFLAGS_INC=-I../shared -I../../shared
CFLAGS_ANY=-pipe
CFLAGS_OPT=-O2 -m486
CFLAGS_WRN=-Wall

CFLAGS=$(CFLAGS_INC) $(CFLAGS_OPT) $(CFLAGS_DEB) $(CFLAGS_WRN) \
       $(CFLAGS_DEF) $(CFLAGS_ANY)

all: crt0.o crossload

crt0.o: crt0.s
	../../ast/ast -o crt0.o crt0.s

crossload: $(OBJS)
	$(CC) $(LDFLAGS) -o crossload $(OBJS) $(LIBS)
	

depend dep:
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

TAGS: force
	etags *.h ../shared/*.h ../../shared/*.h *.c ../shared/*.c

clean:
	-rm -f *.s a.out
distclean: clean
	-rm -f *.o crossload
realclean: distclean

force:

### Dependencies start here -- do not change this line #######
crossload.o: crossload.c ../shared/litload.h ../../shared/shared.h \
 ../../shared/config.h ../../shared/expr.h ../../shared/misc.h \
 ../../shared/cmd.h ../../shared/ments.h ../../shared/litio.h
litload.o: ../shared/litload.c ../shared/litload.h \
 ../../shared/shared.h ../../shared/config.h ../../shared/expr.h \
 ../../shared/misc.h ../../shared/cmd.h ../../shared/ments.h \
 ../../shared/litio.h
