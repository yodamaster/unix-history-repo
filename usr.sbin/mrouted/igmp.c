/*
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE".  Use of the mrouted program represents acceptance of
 * the terms and conditions listed in that file.
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 *
 *
 * igmp.c,v 1.2 1994/09/08 02:51:15 wollman Exp
 */


#include "defs.h"


/*
 * Exported variables.
 */
char		recv_buf[MAX_IP_PACKET_LEN]; /* input packet buffer         */
char		send_buf[MAX_IP_PACKET_LEN]; /* output packet buffer        */
int		igmp_socket;		     /* socket for all network I/O  */
u_long		allhosts_group;		     /* allhosts  addr in net order */
u_long		dvmrp_group;		     /* DVMRP grp addr in net order */
u_long		dvmrp_genid;		     /* IGMP generation id          */

/*
 * Open and initialize the igmp socket, and fill in the non-changing
 * IP header fields in the output packet buffer.
 */
void init_igmp()
{
    struct ip *ip;

    if ((igmp_socket = socket(AF_INET, SOCK_RAW, IPPROTO_IGMP)) < 0)
	log(LOG_ERR, errno, "IGMP socket");

    k_hdr_include(TRUE);	/* include IP header when sending */
    k_set_rcvbuf(48*1024);	/* lots of input buffering        */
    k_set_ttl(1);		/* restrict multicasts to one hop */
    k_set_loop(FALSE);		/* disable multicast loopback     */

    ip         = (struct ip *)send_buf;
    ip->ip_hl  = sizeof(struct ip) >> 2;
    ip->ip_v   = IPVERSION;
    ip->ip_tos = 0;
    ip->ip_off = 0;
    ip->ip_p   = IPPROTO_IGMP;
    ip->ip_ttl = MAXTTL;	/* applies to unicasts only */

    allhosts_group = htonl(INADDR_ALLHOSTS_GROUP);
    dvmrp_group    = htonl(INADDR_DVMRP_GROUP);
}

/* %%% hack for PIM %%% */
#define IGMP_PIM	0x14
#define PIM_QUERY        0
#define PIM_REGISTER     1
#define PIM_REGISTER_STOP 	2
#define PIM_JOIN_PRUNE   3
#define PIM_RP_REACHABLE 4
#define PIM_ASSERT       5
#define PIM_GRAFT        6
#define PIM_GRAFT_ACK    7

static char *packet_kind(type, code)
     u_char type, code;
{
    switch (type) {
	case IGMP_HOST_MEMBERSHIP_QUERY:	return "membership query  ";
	case IGMP_HOST_MEMBERSHIP_REPORT:	return "membership report ";
	case IGMP_HOST_NEW_MEMBERSHIP_REPORT:	return "new membership report ";
	case IGMP_HOST_LEAVE_MESSAGE:                return "leave message";
	case IGMP_DVMRP:
	  switch (code) {
	    case DVMRP_PROBE:	    		return "neighbor probe    ";
	    case DVMRP_REPORT:	    		return "route report      ";
	    case DVMRP_ASK_NEIGHBORS:   	return "neighbor request  ";
	    case DVMRP_NEIGHBORS:	    	return "neighbor list     ";
	    case DVMRP_ASK_NEIGHBORS2:   	return "neighbor request 2";
	    case DVMRP_NEIGHBORS2:	    	return "neighbor list 2   ";
	    case DVMRP_PRUNE:			return "prune message	  ";
	    case DVMRP_GRAFT:			return "graft message	  ";
	    case DVMRP_GRAFT_ACK:		return "graft message ack ";
	    default:	    			return "unknown DVMRP msg ";
	  }
 	case IGMP_PIM:				/* %%% hack for PIM %%% */
 	  switch (code) {
 	    case PIM_QUERY:			return "PIM Router-Query  ";
 	    case PIM_REGISTER:			return "PIM Register      ";
 	    case PIM_REGISTER_STOP:		return "PIM Register-Stop ";
 	    case PIM_JOIN_PRUNE:		return "PIM Join/Prune    ";
 	    case PIM_RP_REACHABLE:		return "PIM RP-Reachable  ";
 	    case PIM_ASSERT:			return "PIM Assert        ";
 	    case PIM_GRAFT:			return "PIM Graft         ";
 	    case PIM_GRAFT_ACK:			return "PIM Graft-Ack     ";
 	    default:		    		return "unknown PIM msg   ";
 	  }
	case IGMP_MTRACE:			return "IGMP trace query  ";
	case IGMP_MTRACE_RESP:			return "IGMP trace reply  ";
	default:			    	return "unknown IGMP msg  ";
    }
}

/*
 * Process a newly received IGMP packet that is sitting in the input
 * packet buffer.
 */
void accept_igmp(recvlen)
    int recvlen;
{
    register u_long src, dst, group;
    struct ip *ip;
    struct igmp *igmp;
    int ipdatalen, iphdrlen, igmpdatalen;

    if (recvlen < sizeof(struct ip)) {
	log(LOG_WARNING, 0,
	    "received packet too short (%u bytes) for IP header", recvlen);
	return;
    }

    ip        = (struct ip *)recv_buf;
    src       = ip->ip_src.s_addr;
    dst       = ip->ip_dst.s_addr;

    /*
     * this is most likely a message from the kernel indicating that
     * a new src grp pair message has arrived and so, it would be
     * necessary to install a route into the kernel for this.
     */
    if (ip->ip_p == 0) {
	if (src == 0 || dst == 0)
	    log(LOG_WARNING, 0, "kernel request not accurate");
	else
	    add_table_entry(src, dst);
	return;
    }

    iphdrlen  = ip->ip_hl << 2;
    ipdatalen = ip->ip_len;
    if (iphdrlen + ipdatalen != recvlen) {
	log(LOG_WARNING, 0,
	    "received packet shorter (%u bytes) than hdr+data length (%u+%u)",
	    recvlen, iphdrlen, ipdatalen);
	return;
    }

    igmp        = (struct igmp *)(recv_buf + iphdrlen);
    group       = igmp->igmp_group.s_addr;
    igmpdatalen = ipdatalen - IGMP_MINLEN;
    if (igmpdatalen < 0) {
	log(LOG_WARNING, 0,
	    "received IP data field too short (%u bytes) for IGMP, from %s",
	    ipdatalen, inet_fmt(src, s1));
	return;
    }

    log(LOG_DEBUG, 0, "RECV %s from %-15s to %s",
	packet_kind(igmp->igmp_type, igmp->igmp_code),
	inet_fmt(src, s1), inet_fmt(dst, s2));

    switch (igmp->igmp_type) {

	case IGMP_HOST_MEMBERSHIP_QUERY:
	    /* we have to do the determination of the querrier router here */
	    return;

	case IGMP_HOST_MEMBERSHIP_REPORT:
	case IGMP_HOST_NEW_MEMBERSHIP_REPORT:
	    accept_group_report(src, dst, group,igmp->igmp_type);
	    return;

	case IGMP_HOST_LEAVE_MESSAGE:
	    leave_group_message(src, dst, group);
	    return;

	case IGMP_DVMRP:
	    switch (igmp->igmp_code) {

		case DVMRP_PROBE:
		    accept_probe(src, dst,
                               (char *)(igmp+1), igmpdatalen, ntohl(group));
		    return;

		case DVMRP_REPORT:
 		    accept_report(src, dst,
                                (char *)(igmp+1), igmpdatalen, ntohl(group));
		    return;

		case DVMRP_ASK_NEIGHBORS:
		    accept_neighbor_request(src, dst);
		    return;

		case DVMRP_ASK_NEIGHBORS2:
		    accept_neighbor_request2(src, dst);
		    return;

		case DVMRP_NEIGHBORS:
		    accept_neighbors(src, dst, (char *)(igmp+1), igmpdatalen,
				     group);
		    return;

		case DVMRP_NEIGHBORS2:
		    accept_neighbors2(src, dst, (char *)(igmp+1), igmpdatalen,
				     group);
		    return;

		case DVMRP_PRUNE:
		    accept_prune(src, dst, (char *)(igmp+1), igmpdatalen);
		    return;

		case DVMRP_GRAFT:
		    accept_graft(src, dst, (char *)(igmp+1), igmpdatalen);
		    return;

		case DVMRP_GRAFT_ACK:
		    accept_g_ack(src, dst, (char *)(igmp+1), igmpdatalen);
		    return;

		default:
		    log(LOG_INFO, 0,
		     "ignoring unknown DVMRP message code %u from %s to %s",
		     igmp->igmp_code, inet_fmt(src, s1),
		     inet_fmt(dst, s2));
		    return;
	    }


 	case IGMP_PIM:		/* %%% hack for PIM  %%% */
 	    return;

	case IGMP_MTRACE:
	    mtrace(src, dst, group, (char *)(igmp+1),
		   igmp->igmp_code, igmpdatalen);
	    return;

	default:
	    log(LOG_INFO, 0,
		"ignoring unknown IGMP message type %u from %s to %s",
		igmp->igmp_type, inet_fmt(src, s1),
		inet_fmt(dst, s2));
	    return;
    }
}


/*
 * Construct an IGMP message in the output packet buffer.  The caller may
 * have already placed data in that buffer, of length 'datalen'.  Then send
 * the message from the interface with IP address 'src' to destination 'dst'.
 */
void send_igmp(src, dst, type, code, group, datalen)
    u_long src, dst;
    int type, code;
    u_long group;
    int datalen;
{
    static struct sockaddr_in sdst = {AF_INET, sizeof sdst};
    struct ip *ip;
    struct igmp *igmp;

    ip                      = (struct ip *)send_buf;
    ip->ip_src.s_addr       = src;
    ip->ip_dst.s_addr       = dst;
    ip->ip_len              = MIN_IP_HEADER_LEN + IGMP_MINLEN + datalen;

    igmp                    = (struct igmp *)(send_buf + MIN_IP_HEADER_LEN);
    igmp->igmp_type         = type;
    igmp->igmp_code         = code;
    igmp->igmp_group.s_addr = group;
    igmp->igmp_cksum        = 0;
    igmp->igmp_cksum        = inet_cksum((u_short *)igmp,
					 IGMP_MINLEN + datalen);

    if (IN_MULTICAST(ntohl(dst))) k_set_if(src);
    if (dst == allhosts_group) k_set_loop(TRUE);

    sdst.sin_addr.s_addr = dst;
    if (sendto(igmp_socket, send_buf, ip->ip_len, 0,
			(struct sockaddr *)&sdst, sizeof(sdst)) < 0) {
	if (errno == ENETDOWN)
	    check_vif_state();
	else
	    log(LOG_WARNING, errno,
		"sendto to %s on %s",
		inet_fmt(dst, s1), inet_fmt(src, s2));
    }

    if (dst == allhosts_group) k_set_loop(FALSE);

    log(LOG_DEBUG, 0, "SENT %s from %-15s to %s",
	packet_kind(type, code), inet_fmt(src, s1), inet_fmt(dst, s2));
}
