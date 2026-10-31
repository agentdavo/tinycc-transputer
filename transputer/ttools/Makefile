# $Id: Makefile,v 1.13 1997/03/29 21:55:24 sizif Exp $

# This file is part of TTOOLS package.
# Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>


### Tunable parameters

# Where to install:

# A directory which appears in the PATH; the tools are installed
# there under native names (ast, lit, ...) for command-line usage.

BINDIR=/usr/local/bin
#BINDIR=/usr/bin

# Cross-tools directory with subdirs: bin, lib, include, ...
# The tools are symlinked there under traditional names (as, ld),
# as GCC expects to see them.

TOOLDIR=/usr/local/t800
#TOOLDIR=/usr/t800

# Where to put manpages

MANDIR=/usr/local/man
#MANDIR=/usr/man

# Where is ar on your system?  We'll use it for target libraries.
# lit can only handle traditional "!<arch>" format; if your ar uses
# a different format, get GNU ar (it's in binutils).

#PATH_AR=`which ar`
PATH_AR=/usr/bin/ar

# Which startup file you want installed as crt0.o?  Pick `absolute'
# if you want ROM-able code, `linkboot' if you want link-bootable
# programs.

#CRT0=startups/absolute/crt0.o
CRT0=startups/linkboot/crt0.a  # yes, crt0.a installed as crt0.o

# Standard places for lit to search for libraries in on this system.
# Leave undefined if you don't want to search in standard places.

LIBDIR='-DLIBDIR=$(TOOLDIR)/lib'
#LLIBDIR='-DLLIBDIR=XXXXX'

# How to make a file link on your system?  Symbolic link is the best,
# if unsupported, resort to hard link or even `cp'.
LN=ln -sf
#LN=ln
#LN=cp

# Want to compress manpages?  Uncomment the desired compressor, or
# `true' if you want uncompressed manpages.
COMPRESS=gzip --best --force
#COMPRESS=compress -f
#COMPRESS=true		# do not compress

# How to run the rcs2log script?  Not needed unless you are going to
# `make dist' of `make changelog'
#
# Note: we want rcs2log only to collect log records for the current
# branch.  This requires to pass `-b' to rlog, but alas, supplying
# rlog options for rcs2log1.2 automatically disables the `since the
# last changelog record' behaviour, which is unaccepable.  Luckily,
# RCS has the RCSINIT backdoor, which we use to pass the `-b' down to
# rlog.

RCS2LOG_WHERE=rcs2log
#RCS2LOG_WHERE=`echo /usr/lib/emacs/*/*/rcs2log|awk '{print $1}'`

RCS2LOG=RCSINIT=-b; export RCSINIT; $(RCS2LOG_WHERE) -h `hostname -f`

# Where to find link.h, the header file for Christoph Niemann's
# transputer link driver for Linux?  (Only needed for oserver, so
# don't bother if you are not going to build it.)

LINK_H_PATH=/usr/src/transputer/driver
#LINK_H_PATH=/usr/include/linux

# CFLAGS components that you might want to override

# 'production' flags
CFLAGS_DEB='-fomit-frame-pointer'
CFLAGS_ANY='-pipe'
CFLAGS_OPT='-O2'
CFLAGS_WRN='-Wall'

# 'development' flags: used for `make devel'
DEVEL_CFLAGS_DEB='-g -DEXPR_DEBUG'
DEVEL_CFLAGS_ANY='-pipe'
DEVEL_CFLAGS_OPT=''
DEVEL_CFLAGS_WRN='-Wall'

### End of tunable parameters


# List of directories to run sub-make's in
DIRS=ast lit litdump dast startups/absolute startups/linkboot

# `make test' uses this list instead of DIRS
TESTDIRS=test/pushpull test/cross-test startups/linkboot

# Tell GNU make to pass on all variables down to subdirectory makes 
.EXPORT_ALL_VARIABLES:

# In case your make is not GNU... at least this will work.
SUBFLAGS=LIBDIR=$(LIBDIR) \
	 LLIBDIR=$(LLIBDIR)		\
	 CFLAGS_DEB=$(CFLAGS_DEB)	\
	 CFLAGS_ANY=$(CFLAGS_ANY)	\
	 CFLAGS_OPT=$(CFLAGS_OPT)	\
	 CFLAGS_WRN=$(CFLAGS_WRN)	\

# Allow `make MAYBETEST= dist' to skip `make test'
MAYBETEST=test


all:
	@echo "Making all in subdirectories..."
	@for d in $(DIRS); do (cd $$d; make $(SUBFLAGS) all) || exit 1; done

devel:
	make "CFLAGS_DEB=$(DEVEL_CFLAGS_DEB)"	\
	     "CFLAGS_ANY=$(DEVEL_CFLAGS_ANY)"	\
	     "CFLAGS_OPT=$(DEVEL_CFLAGS_OPT)"	\
	     "CFLAGS_WRN=$(DEVEL_CFLAGS_WRN)" all

depend dep: force
	@echo "Making depend in subdirectories..."
	@for d in $(DIRS); do (cd $$d; make depend) || exit 1; done

TAGS tags: force
	@echo "Making TAGS in subdirectories..."
	@for d in $(DIRS); do (cd $$d; make TAGS) || exit 1; done
	# Also build overall tagfile, useful for global replacements, etc.
	mkdir ast/temp;
	-mv ast/parse.c ast/scan.c ast/temp;
	-etags ast/*.y ast/*.l ast/*.h lit/*.h shared/*.h startups/shared/*.h \
	       ast/*.c lit/*.c shared/*.c startups/shared/*.c
	-mv ast/temp/* ast;
	rmdir ast/temp

# Note: we "make all" in the distribution directory to be sure that
# everything compiles ok, and that *.[ly] are freshly compiled.
dist: check-committed check-log force
	set -e; \
	VERSION=`sed -e 's,[^"]*",,' -e 's,";,,' shared/version.c`; \
	MYNAME=ttools; \
	DISTDIR=$$MYNAME-$$VERSION; \
	mkdir $$DISTDIR; \
	find . \
	  -name RCS -prune \
	  -o -name CVS -prune \
	  -o -name 'bug*' -prune \
	  -o -name $$MYNAME'-[0-9]*.tar.gz' \
	  -o -name . \
	  -o -type d -name $$MYNAME'-[0-9]*[0-9]' -prune \
	  -o -type d -exec mkdir $$DISTDIR/{} \; \
	  -o -name '*~' \
	  -o -name 'gmon.out' \
	  -o -name '.cvsignore' \
	  -o -name 'xx.*' \
	  -o -print \
	| sed 's,./\(.*\),ln ./\1 '$$DISTDIR'/\1,' \
	| sh; \
	(cd $$DISTDIR && make distclean && make all $(MAYBETEST) distclean) \
	  || exit 1; \
	tar cf - $$DISTDIR \
	| gzip --best > $$MYNAME-$$VERSION.tar.gz; \
	cvs tag -F v`echo $$VERSION|sed 's,[.:],_,'`;

ChangeLog changelog log:  check-committed force
	$(RCS2LOG) > ChangeLog.update
	@if [ -s ChangeLog.update ]; then \
	  cat ChangeLog >> ChangeLog.update; \
	  mv ChangeLog.update ChangeLog; \
	  $(EDITOR) ChangeLog; \
	else \
	  rm ChangeLog.update; \
	  echo "No new log entries"; \
	fi

# Abort if there are uncommitted files
check-committed: force
	@echo "Checking if everything is committed..."
	@if cvs diff >/dev/null; then true; \
	else \
	  echo "You should commit changes first"; \
	  exit 1; \
	fi

# Abort if there is something to be added to ChangeLog.  Can't see an
# easy way to check this automatically, so ask the human...
check-log: force
	@echo "Are you sure ChangeLog is up-to-date?"
	@echo "Hit <Enter> if yes, <Ctrl-C> if not.";
	@read

install: all
	install -s -o root -g bin -m755 ast/ast $(BINDIR)
	install -s -o root -g bin -m755 lit/lit $(BINDIR)
	install -s -o root -g bin -m755 dast/dast $(BINDIR)
	install -s -o root -g bin -m755 litdump/litdump $(BINDIR)
	install -s -o root -g bin -m755 oserver/oserver $(BINDIR)
	#
	[ -d $(TOOLDIR) ] || mkdir $(TOOLDIR)
	[ -d $(TOOLDIR)/bin ] || mkdir $(TOOLDIR)/bin
	[ -d $(TOOLDIR)/lib ] || mkdir $(TOOLDIR)/lib
	[ -d $(TOOLDIR)/include ] || mkdir $(TOOLDIR)/include
	$(LN) $(BINDIR)/ast $(TOOLDIR)/bin/as
	$(LN) $(BINDIR)/lit $(TOOLDIR)/bin/ld
	$(LN) $(PATH_AR) $(TOOLDIR)/bin/ar
	install -o root -g root $(CRT0) -m644 $(TOOLDIR)/lib/crt0.o
	#
	[ -d $(MANDIR) ] || mkdir $(MANDIR)
	[ -d $(MANDIR)/man1 ] || mkdir $(MANDIR)/man1
	[ -d $(MANDIR)/man5 ] || mkdir $(MANDIR)/man5
	install -o root -g root -m644 docs/ast.1 $(MANDIR)/man1
	$(COMPRESS) $(MANDIR)/man1/ast.1
	install -o root -g root -m644 docs/dast.1 $(MANDIR)/man1
	$(COMPRESS) $(MANDIR)/man1/dast.1
	install -o root -g root -m644 docs/lit.1 $(MANDIR)/man1
	$(COMPRESS) $(MANDIR)/man1/lit.1
	install -o root -g root -m644 docs/lit.5 $(MANDIR)/man5
	$(COMPRESS) $(MANDIR)/man5/lit.5
	install -o root -g root -m644 docs/litdump.1 $(MANDIR)/man1
	$(COMPRESS) $(MANDIR)/man1/litdump.1
	install -o root -g root -m644 docs/ttools.1 $(MANDIR)/man1
	$(COMPRESS) $(MANDIR)/man1/ttools.1

test: force
	@echo "Making test in subdirectories..."
	@for d in $(TESTDIRS); do (cd $$d; make test) || exit 1; done

clean:
	@echo "Making clean in subdirectories..."
	@for d in $(DIRS) $(TESTDIRS); do (cd $$d; make clean) || exit 1; done

distclean:
	@echo "Making distclean in subdirectories..."
	@for d in $(DIRS) $(TESTDIRS); do (cd $$d; make distclean) || exit 1; done
	-rm -f TAGS

realclean:
	@echo "Making realclean in subdirectories..."
	@for d in $(DIRS) $(TESTDIRS); do (cd $$d; make realclean) || exit 1; done
	-rm -f TAGS

force:
