# $Id: Makefile,v 1.2 1996/01/16 11:17:43 sizif Exp $

# This file is part of TTOOLS package.
# Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>

SHELL = /bin/sh
VPATH = .:../shared

TTOOLS = ../..
AST = $(TTOOLS)/ast/ast
LIT = $(TTOOLS)/lit/lit
OSERVER = $(TTOOLS)/oserver/oserver

CC = t800-gcc
CFLAGS = -I$(TTOOLS)/shared/ -DNO_LIBC -O

# This stanza is for happy users of GNU make
#CPPFLAGS += -DDEBUG_LITLOAD
#CPPFLAGS += -DDEBUG_PHASE1
#CPPFLAGS += -DDEBUG_PHASE2

# targets for `make test'
TARGETS_BIN = litload.o crt0.o proba
TARGETS_TEXT = litload.s

all: crt0.a proba

crt0.a: crt0.o litload.o
	rm -f crt0.a
	ar q crt0.a crt0.o litload.o

crt0.o: crt0.s
	$(AST) -o crt0.o crt0.s

crt0.s: crt0.S

litload.s: ../shared/litload.c ../shared/litload.h primitives.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -S -o litload.tmp \
	    -include primitives.h ../shared/litload.c
	sed -e 's/\.\(text\|data\|bss\)/.head2/' \
	    -e 's|\.comm \([^,]*\), \([0-9]*\)|.globl \1; \1: .byte ?[\2]|' \
	    litload.tmp > litload.s

litload.o: litload.s
	$(AST) -o litload.o litload.s

proba.s: proba.c
	$(CC) $(CFLAGS) -S proba.c

proba.o: proba.s
	$(AST) -o proba.o proba.s

proba:  proba.o crt0.a
	$(LIT) crt0.a proba.o -o proba
	

# If you have B004 or compatible board installed, and the link driver
# loaded, you can try if all this really works together.  Compare the
# results with the contents of `make.run.out'.
run: proba
	-$(OSERVER) proba
debug-run: proba
	-$(OSERVER) --verbose --hex proba


test: compile compare

compile: clean $(TARGETS_BIN) $(TARGETS_TEXT)

# Compare results of compilation with the stored ones
compare: force
	@exitcode=0; \
	for f in $(TARGETS_BIN) $(TARGETS_TEXT); do \
	  echo "Comparing $$f:"; \
	  cmp store/$$f $$f || exitcode=$$?; \
	done; \
	echo "-----"; \
	exit $$exitcode

# Store the results of compilation for future compares
store accept: force
	@for f in $(TARGETS_BIN) $(TARGETS_TEXT); do \
	  if [ -f $$f ]; \
	  then true; \
	  else \
	    echo "$$f does not exist"; \
	    echo "Run make to create targets first"; \
	    exit 1; \
	  fi; \
	done
	mv $(TARGETS_BIN) $(TARGETS_TEXT) store

depend:
TAGS:
clean:
	rm -f *.o a.out gmon.out proba litload.tmp
distclean: clean
	rm -f crt0.a
realclean: distclean
	rm litload.s
force:
