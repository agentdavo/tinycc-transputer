# $Id: Makefile,v 1.3 1996/01/14 22:06:10 sizif Exp $

# This file is part of TTOOLS package.
# Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>

TTOOLS = ../..
AST = $(TTOOLS)/ast/ast
LIT = $(TTOOLS)/lit/lit
DAST = $(TTOOLS)/dast/dast
LITDUMP = $(TTOOLS)/litdump/litdump
LITLOAD = $(TTOOLS)/startups/absolute/crossload

ASTFLAGS =
LITFLAGS =

# List of files we are going to build:
# ... binary ones
TARGETS_BIN = pushpull.o pushpull pushpull.image
# ... text ones
TARGETS_TEXT = pushpull.s pushpull.o.dump pushpull.o.dump2 \
               pushpull.dump pushpull.dump2 pushpull.das

all: test
test: compile compare

compile: $(TARGETS_BIN) $(TARGETS_TEXT)

pushpull.s: pushpull.S	# no force, ok
	gcc -E pushpull.S > pushpull.s
pushpull.o: force pushpull.s
	$(AST) -o pushpull.o $(ASTFLAGS) pushpull.s
pushpull: force pushpull.o 
	$(LIT) -o pushpull $(LITFLAGS) pushpull.o
pushpull.image: force pushpull
	$(LITLOAD) -o pushpull.image pushpull
pushpull.o.dump: force pushpull
	$(LITDUMP) pushpull.o > pushpull.o.dump
pushpull.o.dump2: force pushpull
	$(LITDUMP) --disassemble head pushpull.o > pushpull.o.dump2
pushpull.dump: force pushpull
	$(LITDUMP) pushpull > pushpull.dump
pushpull.dump2: force pushpull
	$(LITDUMP) --disassemble head pushpull > pushpull.dump2
pushpull.das: force pushpull
	$(DAST) pushpull.image > pushpull.das

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
	rm -f *.o a.out
distclean: clean
	rm -f $(TARGETS_BIN) $(TARGETS_TEXT)
	# avoid preprocessing at user's site -- CPP may be different
	cp store/pushpull.s .
realclean: distclean
force:
