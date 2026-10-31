# $Id: Makefile,v 1.1.1.1 1995/12/22 12:24:40 sizif Exp $

# This file is part of TTOOLS package.
# Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>

TTOOLS = ../..
GCC_SRC = $(TTOOLS)/../../gcc/gcc-2.6.3-t800
GCC_BUILD = $(GCC_SRC)/build-generic

AR = /usr/bin/ar
AST = $(TTOOLS)/ast/ast
LIT = $(TTOOLS)/lit/lit
DAST = $(TTOOLS)/dast/dast
LITDUMP = $(TTOOLS)/litdump/litdump
LITLOAD = $(TTOOLS)/startups/absolute/crossload
CRT0 = $(TTOOLS)/startups/absolute/crt0.o

ARFLAGS = rc
ASTFLAGS = --trace expr
LITFLAGS = --trace expr

# List of files we are going to build:
# ... binary ones
TARGETS_BIN = cross-test.o cross-test cross-test.image
# ... text ones
TARGETS_TEXT = cross-test.o.dump cross-test.o.dump2 \
               cross-test.dump cross-test.dump2 cross-test.das

all: test
test: compile compare

compile: $(TARGETS_BIN) $(TARGETS_TEXT)

cross-test.o: force cross-test.s
	$(AST) -o cross-test.o $(ASTFLAGS) cross-test.s
cross-test: force libgcc.a
	# Many options are unnecessary in this case, but it's close to what
	# linker gets when invoked by gcc
	$(LIT) -o cross-test $(CRT0) -L. -L/usr/t800/lib cross-test.o \
	  -lgcc -lg -lc -lgcc
cross-test.image: force cross-test
	$(LITLOAD) -o cross-test.image cross-test
cross-test.o.dump: force cross-test
	$(LITDUMP) cross-test.o > cross-test.o.dump
cross-test.o.dump2: force cross-test
	$(LITDUMP) --disassemble head cross-test.o > cross-test.o.dump2
cross-test.dump: force cross-test
	$(LITDUMP) cross-test > cross-test.dump
cross-test.dump2: force cross-test
	$(LITDUMP) --disassemble head cross-test > cross-test.dump2
cross-test.das: force cross-test
	$(DAST) cross-test.image > cross-test.das

libgcc.a: force libmembers
	-rm -f tmplibgcc2.a
	for name in `cat libmembers`; do \
	  echo $$name; \
	  $(AST) -o $$name.o $(ASTFLAGS) $$name.s; \
	  if [ $$? -eq 0 ] ; then true; else exit 1; fi; \
	  $(AR) $(ARFLAGS) tmplibgcc2.a $${name}.o; \
	  rm -f $${name}.o; \
	done
	mv tmplibgcc2.a libgcc.a

# Maintenance hook: build assembly-language test code from some
# c code from GCC distribution
source:
	@if [ -x $(GCC_BUILD)/xgcc ]; \
	then true; \
	else \
	  echo "I need xgcc.  I don't insist on *installed* gcc-t800,"; \
	  echo "but you must have it built for 'make source' to work"; \
	  exit 1; \
	fi
	names=`sed -n '/^#if.* L_/s,.*L_,,p' $(GCC_SRC)/libgcc2.c`; \
	echo $$names >> libmembers; \
	for name in $$names; do \
	  echo -n "$$name "; \
	  $(GCC_BUILD)/xgcc -B$(GCC_BUILD)/ -O2 \
	    -Dinhibit_libc -DCROSS_COMPILE -DIN_GCC -DPOSIX \
	    -I$(GCC_BUILD)/include -I$(GCC_BUILD) -I$(GCC_SRC)/config \
	    -o $$name.s -S -DL_$$name $(GCC_SRC)/libgcc2.c; \
	done
	@echo ""
	$(GCC_BUILD)/xgcc -B$(GCC_BUILD)/ -O2 \
	  -DCROSS_COMPILE -DIN_GCC -DPOSIX \
	  -I$(GCC_BUILD)/include \
	  -o cross-test.s -S $(GCC_SRC)/cross-test.c

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

clean:
	rm -f *.[oa] a.out
distclean: clean
	rm -f $(TARGETS_BIN) $(TARGETS_TEXT)
realclean: distclean
	# I don't think we should automate deleting sources
force:
