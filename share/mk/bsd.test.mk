# $FreeBSD$
#
# Generic build infrastructure for test programs.
#
# The code in this file is independent of the implementation of the test
# programs being built; this file just provides generic infrastructure for the
# build and the definition of various helper variables and targets.
#
# Makefiles should never include this file directly.  Instead, they should
# include one of the various *.test.mk depending on the specific test programs
# being built.

.include <bsd.init.mk>

# Pointer to the top directory into which tests are installed.  Should not be
# overriden by Makefiles, but the user may choose to set this in src.conf(5).
TESTSBASE?= /usr/tests

# Directory in which to install tests defined by the current Makefile.
# Makefiles have to override this to point to a subdirectory of TESTSBASE.
TESTSDIR?= .

# Name of the test suite these tests belong to.  Should rarely be changed for
# Makefiles built into the FreeBSD src tree.
TESTSUITE?= FreeBSD

# List of subdirectories containing tests into which to recurse.  This has the
# same semantics as SUBDIR at build-time.  However, the directories listed here
# get registered into the run-time test suite definitions so that the test
# engines know to recurse into these directories.
#
# In other words: list here any directories that contain test programs but use
# SUBDIR for directories that may contain helper binaries and/or data files.
TESTS_SUBDIRS?=

.if !empty(TESTS_SUBDIRS)
SUBDIR+= ${TESTS_SUBDIRS}
.endif

# it is rare for test cases to have man pages
.if !defined(MAN)
WITHOUT_MAN=yes
.export WITHOUT_MAN
.endif

# tell progs.mk we might want to install things
PROG_VARS+= BINDIR
PROGS_TARGETS+= install

beforetest: .PHONY
.if defined(TESTSDIR)
.if ${TESTSDIR} == ${TESTSBASE}
# Forbid running from ${TESTSBASE}.  It can cause false positives/negatives and
# it does not cover all the tests (e.g. it misses testing software in external).
	@echo "*** Sorry, you cannot use make test from src/tests.  Install the"
	@echo "*** tests into their final location and run them from ${TESTSBASE}"
	@false
.else
	@echo "*** Using this test does not preclude you from running the tests"
	@echo "*** installed in ${TESTSBASE}.  This test run may raise false"
	@echo "*** positives and/or false negatives."
.endif
.else
	@echo "*** No TESTSDIR defined; nothing to do."
	@false
.endif
	@echo

.if !target(realtest)
realtest: .PHONY
	@echo "$@ not defined; skipping"
.endif

test: .PHONY
.ORDER: beforetest realtest
test: beforetest realtest

.if target(aftertest)
.ORDER: realtest aftertest
test: aftertest
.endif

.if !empty(SUBDIR)
.include <bsd.subdir.mk>
.endif

.if !empty(PROGS) || !empty(PROGS_CXX) || !empty(SCRIPTS)
.include <bsd.progs.mk>
.elif !empty(FILES)
.include <bsd.files.mk>
.endif

.include <bsd.obj.mk>
