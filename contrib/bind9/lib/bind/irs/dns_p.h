/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * $Id: dns_p.h,v 1.1.206.2 2004/03/17 00:29:48 marka Exp $
 */

#ifndef _DNS_P_H_INCLUDED
#define	_DNS_P_H_INCLUDED

#define	maybe_ok(res, nm, ok) (((res)->options & RES_NOCHECKNAME) != 0U || \
			       (ok)(nm) != 0)
#define maybe_hnok(res, hn) maybe_ok((res), (hn), res_hnok)
#define maybe_dnok(res, dn) maybe_ok((res), (dn), res_dnok)

/*
 * Object state.
 */
struct dns_p {
	void			*hes_ctx;
	struct __res_state	*res;
	void                    (*free_res) __P((void *));
};

/*
 * Methods.
 */

extern struct irs_gr *	irs_dns_gr __P((struct irs_acc *));
extern struct irs_pw *	irs_dns_pw __P((struct irs_acc *));
extern struct irs_sv *	irs_dns_sv __P((struct irs_acc *));
extern struct irs_pr *	irs_dns_pr __P((struct irs_acc *));
extern struct irs_ho *	irs_dns_ho __P((struct irs_acc *));
extern struct irs_nw *	irs_dns_nw __P((struct irs_acc *));

#endif /*_DNS_P_H_INCLUDED*/
