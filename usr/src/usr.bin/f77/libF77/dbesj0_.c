/*-
 * Copyright (c) 1980 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.proprietary.c%
 */

#ifndef lint
static char sccsid[] = "@(#)dbesj0_.c	5.2 (Berkeley) %G%";
#endif /* not lint */

double j0();

double dbesj0_(x)
double *x;
{
	return(j0(*x));
}
