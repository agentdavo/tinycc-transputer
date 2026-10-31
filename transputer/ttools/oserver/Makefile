# $Id: Makefile,v 1.1.1.1 1995/12/22 12:25:10 sizif Exp $

# This file is part of TTOOLS package.
# Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>

SHELL = /bin/sh
VPATH = ../shared

OBJS1 = oserver.o
OBJS2 = version.o
OBJS = $(OBJS1) $(OBJS2)
LIBS =

# Where to find link.h -- the header file for Christoph Niemann's
# transputer link driver for Linux?
#LINK_H_PATH=/usr/include/linux
LINK_H_PATH=/usr/src/transputer/driver

CFLAGS_DEB=-g
CFLAGS_DEF=
CFLAGS_INC=-I../shared -I$(LINK_H_PATH)
CFLAGS_ANY=-pipe
CFLAGS_OPT=-m486
CFLAGS_WRN=-Wall

CFLAGS=$(CFLAGS_INC) $(CFLAGS_OPT) $(CFLAGS_DEB) $(CFLAGS_WRN) \
       $(CFLAGS_DEF) $(CFLAGS_ANY)


all:  oserver

oserver:  $(OBJS)
	$(CC) $(LDFLAGS) -o oserver $(OBJS) $(LIBS)

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
	rm -f *.o a.out Depend

distclean: clean
	rm -f oserver TAGS

realclean: distclean

force:

### Dependencies start here -- do not change this line #######
