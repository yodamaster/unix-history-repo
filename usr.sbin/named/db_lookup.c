#if !defined(lint) && !defined(SABER)
static char sccsid[] = "@(#)db_lookup.c	4.18 (Berkeley) 3/21/91";
static char rcsid[] = "$Id: db_lookup.c,v 1.3 1995/08/20 21:18:24 peter Exp $";
#endif /* not lint */

/*
 * ++Copyright++ 1986
 * -
 * Copyright (c) 1986
 *    The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 * 	This product includes software developed by the University of
 * 	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * -
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * -
 * --Copyright--
 */

/*
 * Table lookup routines.
 */

#include <syslog.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <stdio.h>
#include <ctype.h>

#include "named.h"

/*
 * Lookup 'name' and return a pointer to the namebuf;
 * NULL otherwise. If 'insert', insert name into tables.
 * Wildcard lookups are handled.
 */
struct namebuf *
nlookup(name, htpp, fname, insert)
	const char *name;
	struct hashbuf **htpp;
	const char **fname;
	int insert;
{
	register struct namebuf *np;
	register const char *cp;
	register int c;
	register unsigned hval;
	register struct hashbuf *htp;
	struct namebuf *parent = NULL;
	int escaped = 0;

	htp = *htpp;
	hval = 0;
	*fname = "???";
	for (cp = name; c = *cp++; ) {
		if (!escaped && (c == '.')) {
			parent = np = nlookup(cp, htpp, fname, insert);
			if (np == NULL)
				return (NULL);
			if (*fname != cp)
				return (np);
			if ((htp = np->n_hash) == NULL) {
				if (!insert) {
					if (np->n_dname[0] == '*' &&
					    np->n_dname[1] == '\0')
						*fname = name;
					return (np);
				}
				htp = savehash((struct hashbuf *)NULL);
				np->n_hash = htp;
			}
			*htpp = htp;
			break;
		}
		hval <<= HASHSHIFT;
		hval += (isupper(c) ? tolower(c) : c) & HASHMASK;
		if (escaped)
			escaped = 0;
		else if (c == '\\')
			escaped = 1;
	}
	cp--;
	/*
	 * Lookup this label in current hash table.
	 */
	for (np = htp->h_tab[hval % htp->h_size];
	     np != NULL;
	     np = np->n_next) {
		if (np->n_hashval == hval &&
		    strncasecmp(name, np->n_dname, cp - name) == 0) {
			*fname = name;
			return (np);
		}
	}
	if (!insert) {
		/*
		 * Look for wildcard in this hash table.
		 * Don't use a cached "*" name as a wildcard,
		 * only authoritative.
		 */
		hval = ('*' & HASHMASK) % htp->h_size;
		for (np = htp->h_tab[hval]; np != NULL; np = np->n_next) {
			if (np->n_dname[0] == '*'  && np->n_dname[1] == '\0' &&
			    np->n_data && np->n_data->d_zone != 0) {
				*fname = name;
				return (np);
			}
		}
		return (parent);
	}
	np = savename(name, cp - name);
	np->n_parent = parent;
	np->n_hashval = hval;
	hval %= htp->h_size;
	np->n_next = htp->h_tab[hval];
	htp->h_tab[hval] = np;
	/* Increase hash table size. */
	if (++htp->h_cnt > htp->h_size * 2) {
		*htpp = savehash(htp);
		if (parent == NULL) {
			if (htp == hashtab) {
				hashtab = *htpp;
			} else {
				fcachetab = *htpp;
		        }
		}
		else
			parent->n_hash = *htpp;
		htp = *htpp;
	}
	*fname = name;
	return (np);
}

/* int
 * match(dp, class, type)
 *	Does data record `dp' match the class and type?
 * return value:
 *	boolean
 */
int
match(dp, class, type)
	register struct databuf *dp;
	register int class, type;
{
	dprintf(5, (ddt, "match(0x%lx, %d, %d) %d, %d\n",
		    (u_long)dp, class, type, dp->d_class, dp->d_type));
	if (dp->d_class != class && class != C_ANY)
		return (0);
	if (dp->d_type != type && type != T_ANY)
		return (0);
	return (1);
}
