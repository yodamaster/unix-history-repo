/*-
 * Copyright (c) 1998-2002 Luigi Rizzo, Universita` di Pisa
 * Portions Copyright (c) 2000 Akamba Corp.
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _IP_DUMMYNET_H
#define _IP_DUMMYNET_H

/*
 * Definition of dummynet data structures. In the structures, I decided
 * not to use the macros in <sys/queue.h> in the hope of making the code
 * easier to port to other architectures. The type of lists and queue we
 * use here is pretty simple anyways.
 */

/*
 * We start with a heap, which is used in the scheduler to decide when
 * to transmit packets etc.
 *
 * The key for the heap is used for two different values:
 *
 * 1. timer ticks- max 10K/second, so 32 bits are enough;
 *
 * 2. virtual times. These increase in steps of len/x, where len is the
 *    packet length, and x is either the weight of the flow, or the
 *    sum of all weights.
 *    If we limit to max 1000 flows and a max weight of 100, then
 *    x needs 17 bits. The packet size is 16 bits, so we can easily
 *    overflow if we do not allow errors.
 * So we use a key "dn_key" which is 64 bits. Some macros are used to
 * compare key values and handle wraparounds.
 * MAX64 returns the largest of two key values.
 * MY_M is used as a shift count when doing fixed point arithmetic
 * (a better name would be useful...).
 */
typedef u_int64_t dn_key ;      /* sorting key */
#define DN_KEY_LT(a,b)     ((int64_t)((a)-(b)) < 0)
#define DN_KEY_LEQ(a,b)    ((int64_t)((a)-(b)) <= 0)
#define DN_KEY_GT(a,b)     ((int64_t)((a)-(b)) > 0)
#define DN_KEY_GEQ(a,b)    ((int64_t)((a)-(b)) >= 0)
#define MAX64(x,y)  (( (int64_t) ( (y)-(x) )) > 0 ) ? (y) : (x)
#define MY_M	16 /* number of left shift to obtain a larger precision */

/*
 * XXX With this scaling, max 1000 flows, max weight 100, 1Gbit/s, the
 * virtual time wraps every 15 days.
 */


/*
 * The maximum hash table size for queues.  This value must be a power
 * of 2.
 */
#define DN_MAX_HASH_SIZE 65536

/*
 * A heap entry is made of a key and a pointer to the actual
 * object stored in the heap.
 * The heap is an array of dn_heap_entry entries, dynamically allocated.
 * Current size is "size", with "elements" actually in use.
 * The heap normally supports only ordered insert and extract from the top.
 * If we want to extract an object from the middle of the heap, we
 * have to know where the object itself is located in the heap (or we
 * need to scan the whole array). To this purpose, an object has a
 * field (int) which contains the index of the object itself into the
 * heap. When the object is moved, the field must also be updated.
 * The offset of the index in the object is stored in the 'offset'
 * field in the heap descriptor. The assumption is that this offset
 * is non-zero if we want to support extract from the middle.
 */
struct dn_heap_entry {
    dn_key key ;	/* sorting key. Topmost element is smallest one */
    void *object ;	/* object pointer */
} ;

struct dn_heap {
    int size ;
    int elements ;
    int offset ; /* XXX if > 0 this is the offset of direct ptr to obj */
    struct dn_heap_entry *p ;	/* really an array of "size" entries */
} ;

#ifdef _KERNEL
/*
 * Packets processed by dummynet have an mbuf tag associated with
 * them that carries their dummynet state.  This is used within
 * the dummynet code as well as outside when checking for special
 * processing requirements.
 */
struct dn_pkt_tag {
    uint32_t slot;		/* slot of next rule to use */
    uint32_t rulenum;		/* matching rule number */
    uint32_t rule_id;		/* matching rule id */
    uint32_t chain_id;		/* ruleset id */
    int dn_dir;			/* action when packet comes out. */
#define DN_TO_IP_OUT	1
#define DN_TO_IP_IN	2
/* Obsolete: #define DN_TO_BDG_FWD	3 */
#define DN_TO_ETH_DEMUX	4
#define DN_TO_ETH_OUT	5
#define DN_TO_IP6_IN	6
#define DN_TO_IP6_OUT	7
#define DN_TO_IFB_FWD	8

    dn_key output_time;		/* when the pkt is due for delivery	*/
    struct ifnet *ifp;		/* interface, for ip_output		*/
    struct _ip6dn_args ip6opt;	/* XXX ipv6 options			*/
};
#endif /* _KERNEL */

/*
 * Overall structure of dummynet (with WF2Q+):

In dummynet, packets are selected with the firewall rules, and passed
to two different objects: PIPE or QUEUE.

A QUEUE is just a queue with configurable size and queue management
policy. It is also associated with a mask (to discriminate among
different flows), a weight (used to give different shares of the
bandwidth to different flows) and a "pipe", which essentially
supplies the transmit clock for all queues associated with that
pipe.

A PIPE emulates a fixed-bandwidth link, whose bandwidth is
configurable.  The "clock" for a pipe can come from either an
internal timer, or from the transmit interrupt of an interface.
A pipe is also associated with one (or more, if masks are used)
queue, where all packets for that pipe are stored.

The bandwidth available on the pipe is shared by the queues
associated with that pipe (only one in case the packet is sent
to a PIPE) according to the WF2Q+ scheduling algorithm and the
configured weights.

In general, incoming packets are stored in the appropriate queue,
which is then placed into one of a few heaps managed by a scheduler
to decide when the packet should be extracted.
The scheduler (a function called dummynet()) is run at every timer
tick, and grabs queues from the head of the heaps when they are
ready for processing.

There are three data structures definining a pipe and associated queues:

 + dn_pipe, which contains the main configuration parameters related
   to delay and bandwidth;
 + dn_flow_set, which contains WF2Q+ configuration, flow
   masks, plr and RED configuration;
 + dn_flow_queue, which is the per-flow queue (containing the packets)

Multiple dn_flow_set can be linked to the same pipe, and multiple
dn_flow_queue can be linked to the same dn_flow_set.
All data structures are linked in a linear list which is used for
housekeeping purposes.

During configuration, we create and initialize the dn_flow_set
and dn_pipe structures (a dn_pipe also contains a dn_flow_set).

At runtime: packets are sent to the appropriate dn_flow_set (either
WFQ ones, or the one embedded in the dn_pipe for fixed-rate flows),
which in turn dispatches them to the appropriate dn_flow_queue
(created dynamically according to the masks).

The transmit clock for fixed rate flows (ready_event()) selects the
dn_flow_queue to be used to transmit the next packet. For WF2Q,
wfq_ready_event() extract a pipe which in turn selects the right
flow using a number of heaps defined into the pipe itself.

 *
 */

/*
 * per flow queue. This contains the flow identifier, the queue
 * of packets, counters, and parameters used to support both RED and
 * WF2Q+.
 *
 * A dn_flow_queue is created and initialized whenever a packet for
 * a new flow arrives.
 */
struct dn_flow_queue {
    struct dn_flow_queue *next ;
    struct ipfw_flow_id id ;

    struct mbuf *head, *tail ;	/* queue of packets */
    u_int len ;
    u_int len_bytes ;

    /*
     * When we emulate MAC overheads, or channel unavailability due
     * to other traffic on a shared medium, we augment the packet at
     * the head of the queue with an 'extra_bits' field representsing
     * the additional delay the packet will be subject to:
     *		extra_bits = bw*unavailable_time.
     * With large bandwidth and large delays, extra_bits (and also numbytes)
     * can become very large, so better play safe and use 64 bit
     */
    uint64_t numbytes ;		/* credit for transmission (dynamic queues) */
    int64_t extra_bits;		/* extra bits simulating unavailable channel */

    u_int64_t tot_pkts ;	/* statistics counters	*/
    u_int64_t tot_bytes ;
    u_int32_t drops ;

    int hash_slot ;		/* debugging/diagnostic */

    /* RED parameters */
    int avg ;                   /* average queue length est. (scaled) */
    int count ;                 /* arrivals since last RED drop */
    int random ;                /* random value (scaled) */
    dn_key idle_time;		/* start of queue idle time */

    /* WF2Q+ support */
    struct dn_flow_set *fs ;	/* parent flow set */
    int heap_pos ;		/* position (index) of struct in heap */
    dn_key sched_time ;		/* current time when queue enters ready_heap */

    dn_key S,F ;		/* start time, finish time */
    /*
     * Setting F < S means the timestamp is invalid. We only need
     * to test this when the queue is empty.
     */
} ;

/*
 * flow_set descriptor. Contains the "template" parameters for the
 * queue configuration, and pointers to the hash table of dn_flow_queue's.
 *
 * The hash table is an array of lists -- we identify the slot by
 * hashing the flow-id, then scan the list looking for a match.
 * The size of the hash table (buckets) is configurable on a per-queue
 * basis.
 *
 * A dn_flow_set is created whenever a new queue or pipe is created (in the
 * latter case, the structure is located inside the struct dn_pipe).
 */
struct dn_flow_set {
    SLIST_ENTRY(dn_flow_set)	next;	/* linked list in a hash slot */

    u_short fs_nr ;             /* flow_set number       */
    u_short flags_fs;
#define DN_HAVE_FLOW_MASK	0x0001
#define DN_IS_RED		0x0002
#define DN_IS_GENTLE_RED	0x0004
#define DN_QSIZE_IS_BYTES	0x0008	/* queue size is measured in bytes */
#define DN_NOERROR		0x0010	/* do not report ENOBUFS on drops  */
#define	DN_HAS_PROFILE		0x0020	/* the pipe has a delay profile. */
#define DN_IS_PIPE		0x4000
#define DN_IS_QUEUE		0x8000

    struct dn_pipe *pipe ;	/* pointer to parent pipe */
    u_short parent_nr ;		/* parent pipe#, 0 if local to a pipe */

    int weight ;		/* WFQ queue weight */
    int qsize ;			/* queue size in slots or bytes */
    int plr ;			/* pkt loss rate (2^31-1 means 100%) */

    struct ipfw_flow_id flow_mask ;

    /* hash table of queues onto this flow_set */
    int rq_size ;		/* number of slots */
    int rq_elements ;		/* active elements */
    struct dn_flow_queue **rq;	/* array of rq_size entries */

    u_int32_t last_expired ;	/* do not expire too frequently */
    int backlogged ;		/* #active queues for this flowset */

        /* RED parameters */
#define SCALE_RED               16
#define SCALE(x)                ( (x) << SCALE_RED )
#define SCALE_VAL(x)            ( (x) >> SCALE_RED )
#define SCALE_MUL(x,y)          ( ( (x) * (y) ) >> SCALE_RED )
    int w_q ;			/* queue weight (scaled) */
    int max_th ;		/* maximum threshold for queue (scaled) */
    int min_th ;		/* minimum threshold for queue (scaled) */
    int max_p ;			/* maximum value for p_b (scaled) */
    u_int c_1 ;			/* max_p/(max_th-min_th) (scaled) */
    u_int c_2 ;			/* max_p*min_th/(max_th-min_th) (scaled) */
    u_int c_3 ;			/* for GRED, (1-max_p)/max_th (scaled) */
    u_int c_4 ;			/* for GRED, 1 - 2*max_p (scaled) */
    u_int * w_q_lookup ;	/* lookup table for computing (1-w_q)^t */
    u_int lookup_depth ;	/* depth of lookup table */
    int lookup_step ;		/* granularity inside the lookup table */
    int lookup_weight ;		/* equal to (1-w_q)^t / (1-w_q)^(t+1) */
    int avg_pkt_size ;		/* medium packet size */
    int max_pkt_size ;		/* max packet size */
};
SLIST_HEAD(dn_flow_set_head, dn_flow_set);

/*
 * Pipe descriptor. Contains global parameters, delay-line queue,
 * and the flow_set used for fixed-rate queues.
 *
 * For WF2Q+ support it also has 3 heaps holding dn_flow_queue:
 *   not_eligible_heap, for queues whose start time is higher
 *	than the virtual time. Sorted by start time.
 *   scheduler_heap, for queues eligible for scheduling. Sorted by
 *	finish time.
 *   idle_heap, all flows that are idle and can be removed. We
 *	do that on each tick so we do not slow down too much
 *	operations during forwarding.
 *
 */
struct dn_pipe {		/* a pipe */
    SLIST_ENTRY(dn_pipe)	next;	/* linked list in a hash slot */

    int	pipe_nr ;		/* number	*/
    int bandwidth;		/* really, bytes/tick.	*/
    int	delay ;			/* really, ticks	*/

    struct	mbuf *head, *tail ;	/* packets in delay line */

    /* WF2Q+ */
    struct dn_heap scheduler_heap ; /* top extract - key Finish time*/
    struct dn_heap not_eligible_heap; /* top extract- key Start time */
    struct dn_heap idle_heap ; /* random extract - key Start=Finish time */

    dn_key V ;			/* virtual time */
    int sum;			/* sum of weights of all active sessions */

    /* Same as in dn_flow_queue, numbytes can become large */
    int64_t numbytes;		/* bits I can transmit (more or less). */
    uint64_t burst;		/* burst size, scaled: bits * hz */

    dn_key sched_time ;		/* time pipe was scheduled in ready_heap */
    dn_key idle_time;		/* start of pipe idle time */

    /*
     * When the tx clock come from an interface (if_name[0] != '\0'), its name
     * is stored below, whereas the ifp is filled when the rule is configured.
     */
    char if_name[IFNAMSIZ];
    struct ifnet *ifp ;
    int ready ; /* set if ifp != NULL and we got a signal from it */

    struct dn_flow_set fs ; /* used with fixed-rate flows */

    /* fields to simulate a delay profile */

#define ED_MAX_NAME_LEN		32
    char name[ED_MAX_NAME_LEN];
    int loss_level;
    int samples_no;
    int *samples;
};

/* dn_pipe_max is used to pass pipe configuration from userland onto
 * kernel space and back
 */
#define ED_MAX_SAMPLES_NO	1024
struct dn_pipe_max {
	struct dn_pipe pipe;
	int samples[ED_MAX_SAMPLES_NO];
};

SLIST_HEAD(dn_pipe_head, dn_pipe);

#ifdef _KERNEL

/*
 * Return the dummynet tag; if any.
 * Make sure that the dummynet tag is not reused by lower layers.
 */
static __inline struct dn_pkt_tag *
ip_dn_claim_tag(struct mbuf *m)
{
	struct m_tag *mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
	if (mtag != NULL) {
		mtag->m_tag_id = PACKET_TAG_NONE;
		return ((struct dn_pkt_tag *)(mtag + 1));
	} else
		return (NULL);
}
#endif
#endif /* _IP_DUMMYNET_H */
