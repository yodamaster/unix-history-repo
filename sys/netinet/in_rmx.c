/*
 * Copyright 1994, Massachusetts Institute of Technology.  All Rights Reserved.
 *
 * You may copy this file verbatim until I find the official 
 * Institute boilerplate.
 *
 * $Id: in_rmx.c,v 1.1 1994/11/02 04:42:14 wollman Exp $
 */

/*
 * This code does two things necessary for the enhanced TCP metrics to
 * function in a useful manner:
 *  1) It marks all non-host routes as `cloning', thus ensuring that
 *     every actual reference to such a route actually gets turned
 *     into a reference to a host route to the specific destination
 *     requested.
 *  2) When such routes lose all their references, it arranges for them
 *     to be deleted in some random collection of circumstances, so that
 *     a large quantity of stale routing data is not kept in kernel memory
 *     indefinitely.  See in_rtqtimo() below for the exact mechanism.
 *
 * At least initially, we think that this should have lower overhead than
 * using the existing `expire' mechanism and walking the radix tree
 * periodically, deleting things as we go.  That method would be relatively
 * easy to implement within the framework used here, and in the future
 * we made code both ways, so that folks with large routing tables can use
 * the external queue, and the majority with small routing tables can do
 * the tree-walk.
 */

/*
 * XXX - look for races
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/mbuf.h>

#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>

#define RTPRF_OURS		0x10000	/* set on routes we manage */

/*
 * Do what we need to do when inserting a route.
 */
static struct radix_node *
in_addroute(void *v_arg, void *n_arg, struct radix_node_head *head,
	    struct radix_node *treenodes)
{
	struct rtentry *rt = (struct rtentry *)treenodes;
	struct in_rtq *inr;

	/*
	 * For IP, all non-host routes are automatically cloning.
	 */
	if(!(rt->rt_flags & RTF_HOST))
		rt->rt_flags |= RTF_CLONING;

	return rn_addroute(v_arg, n_arg, head, treenodes);
}

/*
 * This code is the inverse of in_clsroute: on first reference, if we
 * were managing the route, stop doing so and set the expiration timer
 * back off again.
 */
static struct radix_node *
in_matroute(void *v_arg, struct radix_node_head *head)
{
	struct radix_node *rn = rn_match(v_arg, head);
	struct rtentry *rt = (struct rtentry *)rn;

	if(rt && rt->rt_refcnt == 0) { /* this is first reference */
		if(rt->rt_prflags & RTPRF_OURS) {
			rt->rt_prflags &= ~RTPRF_OURS;
			rt->rt_rmx.rmx_expire = 0;
		}
	}
	return rn;
}

#define RTQ_REALLYOLD	4*60*60	/* four hours is ``really old'' */

/*
 * On last reference drop, add the route to the queue so that it can be
 * timed out.
 */
static void
in_clsroute(struct radix_node *rn, struct radix_node_head *head)
{
	struct rtentry *rt = (struct rtentry *)rn;
	struct in_rtq *inr;
	
	if((rt->rt_flags & (RTF_LLINFO | RTF_HOST)) != RTF_HOST)
		return;

	if((rt->rt_prflags & (RTPRF_WASCLONED | RTPRF_OURS)) 
	   != RTPRF_WASCLONED)
		return;

	rt->rt_prflags |= RTPRF_OURS;
	rt->rt_rmx.rmx_expire = time.tv_sec + RTQ_REALLYOLD;
}

/*
 * Get rid of everything in the queue, we are short on memory.
 * Although this looks like an infinite loop, it really isn't;
 * rtrequest() eventually calls in_delroute() which ends up deleting
 * the node at the head (or so we hope).  This should be called from
 * ip_drain().
 */
void
in_rtqdrain(void)
{
	/* write me! */
	;
}

#define RTQ_TIMEOUT	(60*hz)	/* run once a minute */

/*
 * Get rid of old routes.
 */
static void
in_rtqtimo(void *rock)
{
	/* write me! */

	timeout(in_rtqtimo, rock, RTQ_TIMEOUT);
}

/*
 * Initialize our routing tree.
 */
int
in_inithead(void **head, int off)
{
	struct radix_node_head *rnh;

	if(!rn_inithead(head, off))
		return 0;

	rnh = *head;
	rnh->rnh_addaddr = in_addroute;
	rnh->rnh_matchaddr = in_matroute;
	rnh->rnh_close = in_clsroute;
	in_rtqtimo(rnh);	/* kick off timeout first time */
	return 1;
}

