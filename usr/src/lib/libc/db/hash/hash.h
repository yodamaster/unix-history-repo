/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Margo Seltzer.
 *
 * %sccs.include.redist.c%
 *
 *	@(#)hash.h	5.1 (Berkeley) %G%
 */

/* Operations */
typedef enum { HASH_GET, HASH_PUT, HASH_PUTNEW, HASH_DELETE, 
		HASH_FIRST, HASH_NEXT } ACTION;

/* Buffer Management structures */
typedef struct _bufhead BUFHEAD;

struct _bufhead {
    BUFHEAD	*prev;		/* LRU links */
    BUFHEAD	*next;		/* LRU links */
    BUFHEAD	*ovfl;		/* Overflow page buffer header */
    int		addr;		/* Address of this page */
    char	*page;		/* Actual page data */
    char	flags;	/* Combination of BUF_MOD, BUF_DISK, BUF_BUCKET */
};

/* Flag Values */
#define	BUF_MOD		0x0001
#define BUF_DISK	0x0002
#define	BUF_BUCKET	0x0004

#define IS_BUCKET(X)	(X & BUF_BUCKET)

typedef BUFHEAD	**SEGMENT;

/* Hash Table Information */
typedef struct hashhdr {	/* Disk resident portion */
	int magic;	/* Magic NO for hash tables */
	int version;	/* Version ID */
	long lorder;	/* Byte Order */
	int bsize;	/* Bucket/Page Size */
	int bshift;	/* Bucket shift */
	int dsize;	/* Directory Size */
	int ssize;	/* Segment Size */
	int sshift;	/* Segment shift */
	int max_bucket;	/* ID of Maximum bucket in use */
	int high_mask;	/* Mask to modulo into entire table */
	int low_mask;	/* Mask to modulo into lower half of table */
	int ffactor;	/* Fill factor */
	int nkeys;	/* Number of keys in hash table */
	int hdrpages;	/* Size of table header */
	int h_charkey;	/* value of hash(CHARKEY) */
# define NCACHED		32	/* number of bit maps and spare points*/
	int spares[NCACHED];	/* spare pages for overflow */
	u_short bitmaps[NCACHED];	/* address of overflow page bitmaps */
} HASHHDR;

typedef struct htab {	/* Memory resident data structure */
	HASHHDR hdr;	/* Header */
	int nsegs;	/* Number of allocated segments */
	int exsegs;	/* Number of extra allocated segments */
	int (*hash)();	/* Hash Function */
	int flags;	/* Flag values */
	int fp;		/* File pointer */
	char *tmp_buf;	/* Temporary Buffer for BIG data */
	char *tmp_key;	/* Temporary Buffer for BIG keys */
	BUFHEAD *cpage;	/* Current page */
	int cbucket;	/* Current bucket */
	int cndx;  	/* Index of next item on cpage */
	int errno;	/* Error Number -- for DBM compatability */
	int new_file;	/* Indicates whether fd is backing store or no */
	int save_file;	/* Indicates whether we need to flush file at exit */
	u_long *mapp[NCACHED];	/* Pointers to page maps */
	int nbufs;	/* Number of buffers left to allocate */
	BUFHEAD	bufhead; /* Header of buffer lru list */
	SEGMENT	 *dir;	/* Hash Bucket directory */
} HTAB;


/*
 * Constants
 */
#define	MAX_BSIZE		65536	/* 2^16 */
#define MIN_BUFFERS		6
#define MINHDRSIZE		512
#define DEF_BUFSIZE		65536	/* 64 K */
#define DEF_BUCKET_SIZE	256
#define DEF_BUCKET_SHIFT	8	/* log2(BUCKET) */
#define DEF_SEGSIZE		256
#define DEF_SEGSIZE_SHIFT		8      /* log2(SEGSIZE)	 */
#define DEF_DIRSIZE		256
#define DEF_FFACTOR		5
#define SPLTMAX		8
#define CHARKEY		"%$sniglet^&"
#define NUMKEY			1038583
#define VERSION_NO		3
#define BYTE_SHIFT		3
#define INT_TO_BYTE		2
#define INT_BYTE_SHIFT		5
#define ALL_SET		((unsigned)0xFFFFFFFF)
#define ALL_CLEAR		0


#define PTROF(X)	((BUFHEAD *)((unsigned)(X)&~0x3))
#define ISMOD(X)	((unsigned)(X)&0x1)
#define DOMOD(X)	(X = (char *)( (unsigned)X | 0x1))
#define ISDISK(X)	((unsigned)(X)&0x2)
#define DODISK(X)	(X = (char *)( (unsigned)X | 0x2))

#define BITS_PER_MAP    32

/* Given the address of the beginning of a big map, clear/set the nth bit */

#define CLRBIT(A,N) ((A)[N/BITS_PER_MAP] &= ~(1<<(N%BITS_PER_MAP)))
#define SETBIT(A,N) ((A)[N/BITS_PER_MAP] |= (1<<(N%BITS_PER_MAP)))
#define ISSET(A,N) ((A)[N/BITS_PER_MAP] & (1<<(N%BITS_PER_MAP)))

/* Overflow management */
/*
	Overflow page numbers are allocated per split point.
	At each doubling of the table, we can allocate extra
	pages.  So, an overflow page number has the top 5 bits
	indicate which split point and the lower 11 bits indicate
	which page at that split point is indicated (pages within
	split points are numberered starting with 1).


*/

#define SPLITSHIFT	11
#define SPLITMASK	0x7FF
#define SPLITNUM(N)	(((unsigned)N) >> SPLITSHIFT)
#define OPAGENUM(N)	(N & SPLITMASK)
#define	OADDR_OF(S,O)	((S << SPLITSHIFT) + O)

#define BUCKET_TO_PAGE(B) \
	B + hashp->HDRPAGES + (B ? hashp->SPARES[__log2(B+1)-1] : 0)
#define OADDR_TO_PAGE(B) 	\
	BUCKET_TO_PAGE ( (1 << SPLITNUM(B)) -1 ) + OPAGENUM(B);

/*
    page.h contains a detailed description of the page format.

    Normally, keys and data are accessed from offset tables in the
    top of each page which point to the beginning of the key and
    data.  There are four flag values which may be stored in these
    offset tables which indicate the following:

	OVFLPAGE	Rather than a key data pair, this pair contains
			the address of an overflow page.  The format of
			the pair is:
			    OVERFLOW_PAGE_NUMBER OVFLPAGE

	PARTIAL_KEY	This must be the first key/data pair on a page 
			and implies that page contains only a partial key.  
			That is, the key is too big to fit on a single page 
			so it starts on this page and continues on the next.
			The format of the page is:
			    KEY_OFF PARTIAL_KEY OVFL_PAGENO OVFLPAGE
			    
			    KEY_OFF -- offset of the beginning of the key
			    PARTIAL_KEY -- 1
			    OVFL_PAGENO - page number of the next overflow page
			    OVFLPAGE -- 0
	FULL_KEY	This must be the first key/data pair on the page.  It
			is used in two cases.  

			Case 1:
			    There is a complete key on the page but no data
			    (because it wouldn't fit).  The next page contains
			    the data.

			    Page format it:
			    KEY_OFF FULL_KEY OVFL_PAGENO OVFL_PAGE

			    KEY_OFF -- offset of the beginning of the key
			    FULL_KEY -- 2
			    OVFL_PAGENO - page number of the next overflow page
			    OVFLPAGE -- 0

			Case 2:
			    This page contains no key, but part of a large 
			    data field, which is continued on the next page.

			    Page format it:
			    DATA_OFF FULL_KEY OVFL_PAGENO OVFL_PAGE

			    KEY_OFF -- offset of the beginning of the data on
					this page
			    FULL_KEY -- 2
			    OVFL_PAGENO - page number of the next overflow page
			    OVFLPAGE -- 0

	FULL_KEY_DATA	This must be the first key/data pair on the page.
			There are two cases:

			Case 1:
			    This page contains a key and the beginning of the
			    data field, but the data field is continued on the
			    next page.

			    Page format is:
			    KEY_OFF FULL_KEY_DATA OVFL_PAGENO DATA_OFF

			    KEY_OFF -- offset of the beginning of the key
			    FULL_KEY_DATA -- 3
			    OVFL_PAGENO - page number of the next overflow page
			    DATA_OFF -- offset of the beginning of the data 

			Case 2:
			    This page contains the last page of a big data pair.
			    There is no key, only the  tail end of the data 
			    on this page.

			    Page format is:
			    DATA_OFF FULL_KEY_DATA <OVFL_PAGENO> <OVFLPAGE>

			    DATA_OFF -- offset of the beginning of the data on
					this page
			    FULL_KEY_DATA -- 3
			    OVFL_PAGENO - page number of the next overflow page
			    OVFLPAGE -- 0

			    OVFL_PAGENO and OVFLPAGE are optional (they are
			    not present if there is no next page).
*/
#define OVFLPAGE	0
#define PARTIAL_KEY	1
#define FULL_KEY	2
#define FULL_KEY_DATA	3
#define	REAL_KEY	4


/* Short hands for accessing structure */
#define BSIZE	hdr.bsize
#define BSHIFT	hdr.bshift
#define DSIZE	hdr.dsize
#define SGSIZE	hdr.ssize
#define SSHIFT	hdr.sshift
#define LORDER	hdr.lorder
#define MAX_BUCKET	hdr.max_bucket
#define FFACTOR		hdr.ffactor
#define HIGH_MASK	hdr.high_mask
#define LOW_MASK	hdr.low_mask
#define NKEYS		hdr.nkeys
#define HDRPAGES	hdr.hdrpages
#define SPARES		hdr.spares
#define BITMAPS		hdr.bitmaps
#define VERSION		hdr.version
#define MAGIC		hdr.magic
#define NEXT_FREE	hdr.next_free
#define H_CHARKEY	hdr.h_charkey
