/*-
 * Copyright (c) 1985, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * %sccs.include.proprietary.c%
 */

#ifndef lint
static char sccsid[] = "@(#)move.c	8.1 (Berkeley) %G%";
#endif /* not lint */

#include "imp.h"
move(xi,yi){
        imPx = xi;
        imPy = yi;
}
