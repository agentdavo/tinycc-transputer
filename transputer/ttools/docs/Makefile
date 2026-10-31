# $Id: Makefile,v 1.1.1.1 1995/12/22 12:24:39 sizif Exp $

# This file is part of TTOOLS package.
# Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>

# This Makefile exists mainly for maintenance.  There should be no
# need to `make' anything in this directory at user's site.

SHELL=/bin/sh
MAN2HTML=man2html -noheads -topm 6 -botm 6 -cgiurl '$$title.$$section.html'

TTOOLS=..
HTML_MANS=ast.1.html dast.1.html lit.1.html lit.5.html \
	litdump.1.html ttools.1.html

all: $(HTML_MANS) ttools-README

ast.1.html: ast.1
	nroff -man ast.1 | $(MAN2HTML) >ast.1.html
dast.1.html: dast.1
	nroff -man dast.1 | $(MAN2HTML) >dast.1.html
lit.1.html: lit.1
	nroff -man lit.1 | $(MAN2HTML) >lit.1.html
lit.5.html: lit.5
	nroff -man lit.5 | $(MAN2HTML) >lit.5.html
litdump.1.html: litdump.1
	nroff -man litdump.1 | $(MAN2HTML) >litdump.1.html
ttools.1.html: ttools.1
	nroff -man ttools.1 | $(MAN2HTML) >ttools.1.html

ttools-README: $(TTOOLS)/README
	cp $(TTOOLS)/README ttools-README

install: all
	rcp *.html ttools-README www:www/ttools

clean:
	-rm -f *.[0-9].html
distclean: clean
realclean: distclean
force:
