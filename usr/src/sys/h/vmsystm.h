/*	vmsystm.h	4.1	11/9/80	*/

/*
 * Miscellaneous virtual memory subsystem variables and structures.
 */


#ifdef KERNEL
int	freemem;		/* remaining blocks of free memory */
int	avefree;		/* moving average of remaining free blocks */
int	deficit;		/* estimate of needs of new swapped in procs */
int	nscan;			/* number of scans in last second */
int	multprog;		/* current multiprogramming degree */
int	desscan;		/* desired pages scanned per second */

/* writable copies of tunables */
int	maxpgio;		/* max paging i/o per sec before start swaps */
int	maxslp;			/* max sleep time before very swappable */
int	lotsfree;		/* max free before clock freezes */
int	minfree;		/* minimum free pages before swapping begins */
int	desfree;		/* no of pages to try to keep free via daemon */
int	saferss;		/* no pages not to steal; decays with slptime */
#endif

/*
 * Fork/vfork accounting.
 */
struct	forkstat
{
	int	cntfork;
	int	cntvfork;
	int	sizfork;
	int	sizvfork;
};
#ifdef KERNEL
struct	forkstat forkstat;
#endif

/*
 * Swap kind accounting.
 */
struct	swptstat
{
	int	pteasy;		/* easy pt swaps */
	int	ptexpand;	/* pt expansion swaps */
	int	ptshrink;	/* pt shrinking swaps */
	int	ptpack;		/* pt swaps involving spte copying */
};
#ifdef KERNEL
struct	swptstat swptstat;
#endif
