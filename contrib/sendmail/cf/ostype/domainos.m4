divert(-1)
#
# Copyright (c) 1998, 1999 Sendmail, Inc. and its suppliers.
#	All rights reserved.
# Copyright (c) 1983 Eric P. Allman.  All rights reserved.
# Copyright (c) 1988, 1993
#	The Regents of the University of California.  All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)
VERSIONID(`$Id: domainos.m4,v 8.14 1999/04/24 05:37:40 gshapiro Exp $')
divert(-1)

ifdef(`QUEUE_DIR',, `define(`QUEUE_DIR', /usr/spool/mqueue)')
define(`confEBINDIR', `/usr/lib')dnl
